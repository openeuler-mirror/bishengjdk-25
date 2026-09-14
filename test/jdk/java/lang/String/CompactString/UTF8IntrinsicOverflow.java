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

/*
 * @test
 * @summary Verify UTF-16 to UTF-8 intrinsic allocation overflow handling
 * @requires os.arch == "aarch64" & os.maxMemory >= 8g & vm.bits == 64
 * @run main/othervm/timeout=600 -Xmx8g -Xbatch -XX:-TieredCompilation -XX:+UseUTFConversionIntrinsics UTF8IntrinsicOverflow
 */

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public class UTF8IntrinsicOverflow {
    private static final int MAX_ARRAY_LENGTH = Integer.MAX_VALUE;
    private static final int INTRINSIC_PADDING = 16;
    private static final int INTRINSIC_ITERATIONS = 20_000;

    public static void main(String[] args) {
        testIntrinsicRange();
        testPaddingExceedsLimitButExactLengthFits();
        testExactLengthExceedsLimit();
    }

    private static void testIntrinsicRange() {
        String source = "\uFFFF".repeat(24);
        byte[] expected = new byte[24 * 3];
        Arrays.fill(expected, (byte) 0xEF);
        for (int i = 0; i < expected.length; i += 3) {
            expected[i + 1] = (byte) 0xBF;
            expected[i + 2] = (byte) 0xBF;
        }
        for (int i = 0; i < INTRINSIC_ITERATIONS; i++) {
            byte[] actual = source.getBytes(StandardCharsets.UTF_8);
            check(Arrays.equals(expected, actual), "intrinsic-range encoding mismatch");
        }
    }

    private static void testPaddingExceedsLimitButExactLengthFits() {
        int length = (MAX_ARRAY_LENGTH - INTRINSIC_PADDING) / 3 + 1;
        String source = "\u0100" + "A".repeat(length - 1);
        byte[] encoded = source.getBytes(StandardCharsets.UTF_8);
        check(encoded.length == length + 1,
                "slowpath exact-length encoding failed: " + encoded.length);
        check((encoded[0] & 0xff) == 0xc4 && (encoded[1] & 0xff) == 0x80,
                "non-ASCII prefix was not encoded correctly");
        check(encoded[2] == 'A' && encoded[encoded.length - 1] == 'A',
                "ASCII suffix was not encoded correctly");
        System.gc();
    }

    private static void testExactLengthExceedsLimit() {
        int length = MAX_ARRAY_LENGTH / 3 + 1;
        String source = "\uFFFF".repeat(length);
        try {
            source.getBytes(StandardCharsets.UTF_8);
            throw new AssertionError("expected OutOfMemoryError");
        } catch (OutOfMemoryError error) {
            String message = error.getMessage();
            check(message != null && message.startsWith("Required length exceeds implementation limit"),
                    "unexpected OutOfMemoryError: " + message);
        } catch (NegativeArraySizeException error) {
            throw new AssertionError("intrinsic allocation overflow leaked NegativeArraySizeException", error);
        }
    }

    private static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }
}
