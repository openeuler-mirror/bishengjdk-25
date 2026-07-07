/*
 * Copyright (c) 2003, 2025, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014, Red Hat Inc. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "cds/cdsConfig.hpp"
#include "os_linux.inline.hpp"
#include "runtime/arguments.hpp"
#include "runtime/flags/jvmFlag.hpp"
#include "runtime/flags/jvmFlagAccess.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/javaThread.hpp"
#include "runtime/globals_extension.hpp"
#include "utilities/defaultStream.hpp"
#include "utilities/debug.hpp"

#include <sys/file.h>

frame JavaThread::pd_last_frame() {
  assert(has_last_Java_frame(), "must have last_Java_sp() when suspended");
  frame f = frame(_anchor.last_Java_sp(), _anchor.last_Java_fp(), _anchor.last_Java_pc());
  f.set_sp_is_trusted();
  return f;
}

// For Forte Analyzer AsyncGetCallTrace profiling support - thread is
// currently interrupted by SIGPROF
bool JavaThread::pd_get_top_frame_for_signal_handler(frame* fr_addr,
  void* ucontext, bool isInJava) {

  assert(Thread::current() == this, "caller must be current thread");
  return pd_get_top_frame(fr_addr, ucontext, isInJava);
}

bool JavaThread::pd_get_top_frame_for_profiling(frame* fr_addr, void* ucontext, bool isInJava) {
  return pd_get_top_frame(fr_addr, ucontext, isInJava);
}

bool JavaThread::pd_get_top_frame(frame* fr_addr, void* ucontext, bool isInJava) {
  // If we have a last_Java_frame, then we should use it even if
  // isInJava == true.  It should be more reliable than ucontext info.
  if (has_last_Java_frame() && frame_anchor()->walkable()) {
    *fr_addr = pd_last_frame();
    return true;
  }

  // At this point, we don't have a last_Java_frame, so
  // we try to glean some information out of the ucontext
  // if we were running Java code when SIGPROF came in.
  if (isInJava) {
    ucontext_t* uc = (ucontext_t*) ucontext;

    intptr_t* ret_fp;
    intptr_t* ret_sp;
    address addr = os::fetch_frame_from_context(uc, &ret_sp, &ret_fp);
    if (addr == nullptr || ret_sp == nullptr ) {
      // ucontext wasn't useful
      return false;
    }

    frame ret_frame(ret_sp, ret_fp, addr);
    if (!ret_frame.safe_for_sender(this)) {
#ifdef COMPILER2
      frame ret_frame2(ret_sp, nullptr, addr);
      if (!ret_frame2.safe_for_sender(this)) {
        // nothing else to try if the frame isn't good
        return false;
      }
      ret_frame = ret_frame2;
#else
      // nothing else to try if the frame isn't good
      return false;
#endif /* COMPILER2 */
    }
    *fr_addr = ret_frame;
    return true;
  }

  // nothing else to try
  return false;
}

void JavaThread::cache_global_variables() { }

static char* get_java_executable_path() {
  const char* java_home = Arguments::get_property("java.home");
  if (java_home != NULL) {
    char* path = NEW_C_HEAP_ARRAY(char, MAXPATHLEN, mtInternal);
    jio_snprintf(path, MAXPATHLEN, "%s/bin/java", java_home);
    return path;
  }
  return os::strdup("java");
}

static bool can_read_classlist(const char* class_list_path) {
  int fd = open(class_list_path, O_RDWR | O_CREAT, 0644);
  if (fd < 0) return false;
  return flock(fd, LOCK_EX | LOCK_NB) == 0;
}

static void construct_path(char *dest, size_t dest_size, const char *base, const char *suffix) {
  size_t base_len = strlen(base);
  size_t suffix_len = strlen(suffix);
  guarantee(base_len + suffix_len < dest_size, "base path too long!");

  jio_snprintf(dest, dest_size, "%s%s", base, suffix);
}

static const char* get_compressed_oops_suffix() {
  return UseCompressedOops ? "_coop" : "_nocoop";
}

static void create_jsa_with_coop_option(const char* class_list_path, const char* appcds_path,
                                         const JavaVMInitArgs* original_args, bool use_compressed_oops) {
  (void)original_args;
  pid_t pid = fork();
  if (pid == 0) {
    // child process running on background
    setsid();
    signal(SIGHUP, SIG_IGN);
    const char* classpath = Arguments::get_appclasspath();
    if (classpath == NULL) {
      classpath = ".";
    }
    char* java_path = get_java_executable_path();
    int arg_count = Arguments::num_jvm_args();
    char** vm_args  = Arguments::jvm_args_array();

    int total_args = arg_count + 12;
    char** args = NEW_C_HEAP_ARRAY(char*, total_args + 1, mtInternal);
    int idx = 0;

    args[idx++] = java_path;
    args[idx++] = os::strdup("-Xshare:dump");
    args[idx++] = os::strdup("-XX:+UnlockDiagnosticVMOptions");
    args[idx++] = os::strdup("-XX:+SkipSharedClassPathCheck");

    char shared_class_list_file[PATH_MAX];
    construct_path(shared_class_list_file, sizeof(shared_class_list_file), "-XX:SharedClassListFile=", class_list_path);
    args[idx++] = os::strdup(shared_class_list_file);

    char shared_archive_file[PATH_MAX];
    construct_path(shared_archive_file, sizeof(shared_archive_file), "-XX:SharedArchiveFile=", appcds_path);
    args[idx++] = os::strdup(shared_archive_file);

    args[idx++] = os::strdup("-classpath");
    args[idx++] = os::strdup(classpath);

    // copy the original parameters and filter out the conflicting ones
    for (int i = 0; i < arg_count; i++) {
      if (vm_args[i] != NULL && strstr(vm_args[i], "AutoSharedArchivePath") == NULL
                             && strstr(vm_args[i], "UseCompressedOops") == NULL) {
        args[idx++] = os::strdup(vm_args[i]);
      }
    }

    args[idx++] = os::strdup("-Xms128m");
    args[idx++] = os::strdup("-Xmx256m");

    if (use_compressed_oops) {
      args[idx++] = os::strdup("-XX:+UseCompressedOops");
    } else {
      args[idx++] = os::strdup("-XX:-UseCompressedOops");
    }

    args[idx++] = os::strdup("-version");
    args[idx] = NULL;

    if (PrintAutoAppCDS) {
      tty->print_cr("Creating JSA with UseCompressedOops=%s", use_compressed_oops ? "true" : "false");
      int i = 0;
      while (args[i] != NULL) {
        tty->print_cr("args[%d] = %s", i, args[i]);
        i++;
      }
    }
    execv(java_path, args);
  }
}

// create missing JSA files (both coop and nocoop versions if needed)
static void try_create_missing_jsa(const char* class_list_path, const char* base_path,
                                    const JavaVMInitArgs* original_args) {
  (void)original_args;
  char appcds_coop_path[PATH_MAX];
  char appcds_nocoop_path[PATH_MAX];

  construct_path(appcds_coop_path, sizeof(appcds_coop_path), base_path, "/appcds_coop.jsa");
  construct_path(appcds_nocoop_path, sizeof(appcds_nocoop_path), base_path, "/appcds_nocoop.jsa");

  struct stat st;
  bool coop_exists = (stat(appcds_coop_path, &st) == 0);
  bool nocoop_exists = (stat(appcds_nocoop_path, &st) == 0);

  if (!coop_exists) {
    if (PrintAutoAppCDS) {
      tty->print_cr("Creating missing JSA file with compressed oops: %s", appcds_coop_path);
    }
    create_jsa_with_coop_option(class_list_path, appcds_coop_path, original_args, true);
  }

  if (!nocoop_exists) {
    if (PrintAutoAppCDS) {
      tty->print_cr("Creating missing JSA file without compressed oops: %s", appcds_nocoop_path);
    }
    create_jsa_with_coop_option(class_list_path, appcds_nocoop_path, original_args, false);
  }
}

void JavaThread::handle_appcds_for_executor(const JavaVMInitArgs* args) {
  if (FLAG_IS_DEFAULT(AutoSharedArchivePath)) {
    return;
  }

  if (AutoSharedArchivePath == NULL) {
    warning("AutoSharedArchivePath should not be empty. Please set the specific path.");
    return;
  }

  static char base_path[JVM_MAXPATHLEN] = {'\0'};
  jio_snprintf(base_path, sizeof(base_path), "%s", AutoSharedArchivePath);

  struct stat st;
  if (stat(base_path, &st) != 0) {
    if (mkdir(base_path, 0755) != 0) {
      vm_exit_during_initialization(err_msg("Can't create dirs %s : %s", base_path, os::strerror(errno)));
    }
  } else {
    if (!S_ISDIR(st.st_mode)) {
      vm_exit_during_initialization(err_msg("Path %s exists but is not a directory.", base_path));
    }

    if (access(base_path, R_OK | W_OK | X_OK) != 0) {
      vm_exit_during_initialization(err_msg("Insufficient permissions for directory %s. Requires read,"
                                    " write, and execute access. Error: %s", base_path, os::strerror(errno)));
    }
  }

  char class_list_path[PATH_MAX];
  char current_appcds_path[PATH_MAX];
  char current_aggrecds_path[PATH_MAX];

  construct_path(class_list_path, sizeof(class_list_path), base_path, "/appcds.lst");

  // build the current JSA file name based on UseCompressedOops
  const char* suffix = get_compressed_oops_suffix();
  char appcds_filename[64];
  char aggrecds_filename[64];
  jio_snprintf(appcds_filename, sizeof(appcds_filename), "/appcds%s.jsa", suffix);
  jio_snprintf(aggrecds_filename, sizeof(aggrecds_filename), "/aggre%s.jsa", suffix);

  construct_path(current_appcds_path, sizeof(current_appcds_path), base_path, appcds_filename);
  construct_path(current_aggrecds_path, sizeof(current_aggrecds_path), base_path, aggrecds_filename);

  if (PrintAutoAppCDS) {
    tty->print_cr("classlist file : %s", class_list_path);
    tty->print_cr("appcds jsa file : %s", current_appcds_path);
    tty->print_cr("UseCompressedOops : %s", UseCompressedOops ? "true" : "false");
    if (UseAggressiveCDS) {
      tty->print_cr("aggressive jsa file : %s", current_aggrecds_path);
    }
  }

  const char* class_list_ptr = class_list_path;
  const char* appcds_ptr = current_appcds_path;
  const char* aggrecds_ptr = current_aggrecds_path;

  if (UseAggressiveCDS) {
    if (stat(current_aggrecds_path, &st) == 0) {
      if (PrintAutoAppCDS) {
        tty->print_cr("Use Aggressive JSA: %s\n", current_aggrecds_path);
      }

      UnlockDiagnosticVMOptions = true;
      UnlockExperimentalVMOptions = true;
      UseSharedSpaces = true;
      RequireSharedSpaces = true;
      SkipSharedClassPathCheck = true;
      JVMFlagAccess::set_ccstr(JVMFlag::find_declared_flag((char*)"SharedArchiveFile"), &aggrecds_ptr, JVMFlagOrigin::COMMAND_LINE);
      return;
    }
  }

  if (stat(current_appcds_path, &st) == 0) {
    if (UseAggressiveCDS) {
      if (PrintAutoAppCDS) {
        tty->print_cr("Generate Aggressive JSA from: %s\n", current_appcds_path);
      }
    }

    try_create_missing_jsa(class_list_path, base_path, args);

    UnlockDiagnosticVMOptions = true;
    UnlockExperimentalVMOptions = true;
    UseSharedSpaces = true;
    RequireSharedSpaces = true;
    SkipSharedClassPathCheck = true;
    JVMFlagAccess::set_ccstr(JVMFlag::find_declared_flag((char*)"SharedArchiveFile"), &appcds_ptr, JVMFlagOrigin::COMMAND_LINE);
    if (UseAggressiveCDS) {
      JVMFlagAccess::set_ccstr(JVMFlag::find_declared_flag((char*)"ArchiveClassesAtExit"), &aggrecds_ptr, JVMFlagOrigin::COMMAND_LINE);
      CDSConfig::enable_dumping_dynamic_archive(ArchiveClassesAtExit);
    }
    return;
  }

  if (stat(class_list_path, &st) == 0) {
    if (!can_read_classlist(class_list_path)) {
      if (PrintAutoAppCDS) {
        tty->print_cr("classlist is generating, can't create jsa by %d now.", os::current_process_id());
      }
      return;
    }

    // generate two versions of JSA
    if (stat(current_appcds_path, &st) != 0) {
      if (PrintAutoAppCDS) {
        tty->print_cr("generate JSA files by %d.", os::current_process_id());
      }
      try_create_missing_jsa(class_list_path, base_path, args);
    }
  } else {
    if (!can_read_classlist(class_list_path)) {
      return;
    }
    if (PrintAutoAppCDS) {
      tty->print_cr("generate classlist file by %d.", os::current_process_id());
    }
    if (NUMANodesRandom != 0) {
      NUMANodesRandom = 0;
    }
    UseSharedSpaces = false;
    RequireSharedSpaces = false;
    JVMFlagAccess::set_ccstr(JVMFlag::find_declared_flag((char*)"DumpLoadedClassList"), &class_list_ptr, JVMFlagOrigin::COMMAND_LINE);
  }
}
