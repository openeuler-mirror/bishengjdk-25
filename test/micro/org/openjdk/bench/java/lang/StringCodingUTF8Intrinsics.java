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

package org.openjdk.bench.java.lang;

import org.openjdk.jmh.annotations.*;

@BenchmarkMode(Mode.AverageTime)
@Warmup(iterations = 3)
@Measurement(iterations = 5, time = 5)
@Threads(1)
public class StringCodingUTF8Intrinsics {

    private static int count = 100000;

    private static byte[][] ss = {
            "dqwqdwqwqdw".getBytes(),
            "了这篇文章：".getBytes(),
            "[  ]hzzone:也就垄断平台总部工资跟老人退休金涨其他收入大多下跌或失业归零(2026-02-06 07:20)\n".getBytes(),
            "[-5]voTvo:前提就说错了，收入越来越低好吗(2026-02-06 10:16)\n".getBytes(),
            "[-5]shocker:财富集中和宏大叙事，和万千普通家庭感受相矛盾。(2026-02-06 10:22)\n".getBytes(),
            "[-5]yourcarin0:预制菜事件忘了？(2026-02-06 11:14)\n".getBytes(),
            "[+5]whatswrong:单次单人费用涨了，下馆子频率大幅降低(2026-02-06 14:13)".getBytes()
    };

    @Benchmark
    public static void TT() {
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < ss.length; j++) {
                String s = new String(ss[j]);
                byte[] df = s.getBytes();
                df[df.length - 1] = df[df.length - 2];
            }
        }
    }

    public static void main(String[] args) {
        count = 1;
        TT();
    }
}
