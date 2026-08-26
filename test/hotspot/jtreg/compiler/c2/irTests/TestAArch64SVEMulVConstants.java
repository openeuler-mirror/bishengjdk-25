/*
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
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
 */

package compiler.c2.irTests;

import compiler.lib.ir_framework.*;
import jdk.test.lib.Asserts;

/*
 * @test
 * @summary Test AArch64 SVE MulV with reusable broadcast constants.
 * @requires os.arch == "aarch64" & vm.compiler2.enabled & vm.cpu.features ~= ".*sve.*"
 * @library /test/lib /
 * @run driver compiler.c2.irTests.TestAArch64SVEMulVConstants
 */

public class TestAArch64SVEMulVConstants {
    private static final int SIZE = 3000;

    // vmul*_sve destroys its first input. Reject this matched shape:
    //   vmul*_sve === _ <replicate-node> <data-node>
    private static final String SVE_MUL_WITH_REPLICATE_AS_FIRST_INPUT =
            "(?ms)^\\s*\\d+\\s+vmul[BSI]_sve\\s+===\\s+_\\s+(\\d+)\\b.*?" +
            "^\\s*\\1\\s+replicateI\\b";

    private static byte BYTE_FACTOR = -7;
    private static short SHORT_FACTOR = -311;
    private static int INT_FACTOR = -104729;

    private static final byte[] BYTE_A = new byte[SIZE];
    private static final byte[] BYTE_B = new byte[SIZE];
    private static final byte[] BYTE_D1 = new byte[SIZE];
    private static final byte[] BYTE_D2 = new byte[SIZE];

    private static final short[] SHORT_A = new short[SIZE];
    private static final short[] SHORT_B = new short[SIZE];
    private static final short[] SHORT_D1 = new short[SIZE];
    private static final short[] SHORT_D2 = new short[SIZE];

    private static final int[] INT_A = new int[SIZE];
    private static final int[] INT_B = new int[SIZE];
    private static final int[] INT_D1 = new int[SIZE];
    private static final int[] INT_D2 = new int[SIZE];

    public static void main(String[] args) {
        for (int i = 0; i < SIZE; i++) {
            BYTE_A[i] = (byte) (i * 7 - 120);
            BYTE_B[i] = (byte) (90 - i * 5);
            SHORT_A[i] = (short) (i * 19 - 30000);
            SHORT_B[i] = (short) (25000 - i * 23);
            INT_A[i] = i * 0x12345 - 0x5678_9abc;
            INT_B[i] = 0x1357_2468 - i * 0x54321;
        }
        TestFramework.runWithFlags("-XX:UseSVE=1", "-XX:MaxVectorSize=32");
    }

    @Test
    @IR(applyIfAnd = {"UseSVE", "> 0", "MaxVectorSize", "32"},
        counts = {IRNode.MUL_VB, ">0"})
    @IR(applyIfAnd = {"UseSVE", "> 0", "MaxVectorSize", "32"},
        failOn = {SVE_MUL_WITH_REPLICATE_AS_FIRST_INPUT},
        counts = {"vmulB_sve", ">0"},
        phase = CompilePhase.MATCHING)
    private static void testByteConstantOnLeft() {
        for (int i = 0; i < SIZE; i++) {
            BYTE_D1[i] = (byte) (BYTE_FACTOR * BYTE_A[i]);
            BYTE_D2[i] = (byte) (BYTE_FACTOR * BYTE_B[i]);
        }
    }

    @Test
    @IR(applyIfAnd = {"UseSVE", "> 0", "MaxVectorSize", "32"},
        counts = {IRNode.MUL_VS, ">0"})
    @IR(applyIfAnd = {"UseSVE", "> 0", "MaxVectorSize", "32"},
        failOn = {SVE_MUL_WITH_REPLICATE_AS_FIRST_INPUT},
        counts = {"vmulS_sve", ">0"},
        phase = CompilePhase.MATCHING)
    private static void testShortConstantOnLeft() {
        for (int i = 0; i < SIZE; i++) {
            SHORT_D1[i] = (short) (SHORT_FACTOR * SHORT_A[i]);
            SHORT_D2[i] = (short) (SHORT_FACTOR * SHORT_B[i]);
        }
    }

    @Test
    @IR(applyIfAnd = {"UseSVE", "> 0", "MaxVectorSize", "32"},
        counts = {IRNode.MUL_VI, ">0"})
    @IR(applyIfAnd = {"UseSVE", "> 0", "MaxVectorSize", "32"},
        failOn = {SVE_MUL_WITH_REPLICATE_AS_FIRST_INPUT},
        counts = {"vmulI_sve", ">0"},
        phase = CompilePhase.MATCHING)
    private static void testIntConstantOnLeft() {
        for (int i = 0; i < SIZE; i++) {
            INT_D1[i] = INT_FACTOR * INT_A[i];
            INT_D2[i] = INT_FACTOR * INT_B[i];
        }
    }

    @Run(test = {"testByteConstantOnLeft", "testShortConstantOnLeft", "testIntConstantOnLeft"})
    private void run() {
        testByteConstantOnLeft();
        for (int i = 0; i < SIZE; i++) {
            Asserts.assertEquals(BYTE_D1[i], (byte) (BYTE_FACTOR * BYTE_A[i]));
            Asserts.assertEquals(BYTE_D2[i], (byte) (BYTE_FACTOR * BYTE_B[i]));
        }

        testShortConstantOnLeft();
        for (int i = 0; i < SIZE; i++) {
            Asserts.assertEquals(SHORT_D1[i], (short) (SHORT_FACTOR * SHORT_A[i]));
            Asserts.assertEquals(SHORT_D2[i], (short) (SHORT_FACTOR * SHORT_B[i]));
        }

        testIntConstantOnLeft();
        for (int i = 0; i < SIZE; i++) {
            Asserts.assertEquals(INT_D1[i], INT_FACTOR * INT_A[i]);
            Asserts.assertEquals(INT_D2[i], INT_FACTOR * INT_B[i]);
        }
    }
}
