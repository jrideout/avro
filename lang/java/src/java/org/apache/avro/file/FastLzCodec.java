/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.apache.avro.file;

import java.io.ByteArrayOutputStream;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import org.jfastlz.JFastLZLevel;
import org.jfastlz.JFastLZPack;
import org.jfastlz.JFastLZUnpack;

import org.apache.avro.io.BinaryDecoder;
import org.apache.avro.io.Decoder;

/** 
 * Implements FastLZ compression and decompression. 
 *
 */
class FastLzCodec extends Codec {

  private static final int DEFAULT_COMPRESSION_LEVEL = -1;

  static class Option extends CodecFactory {
    private int compressionLevel;

    public Option(int compressionLevel) {
      this.compressionLevel = compressionLevel;
    }

    public Option() {
      this.compressionLevel = DEFAULT_COMPRESSION_LEVEL;
    }

    @Override
    protected Codec createInstance() {
      return new DeflateCodec(compressionLevel);
    }
  }

  ByteArrayOutputStream compressionBuffer;
  private int compressionLevel;
  private JFastLZPack compressor;
  private JFastLZUnpack decompressor;

  public FastLzCodec(int compressionLevel) {
    this.compressionLevel = compressionLevel;
  }

  @Override
  String getName() {
    return DataFileConstants.FASTLZ_CODEC;
  }

  @Override
  ByteArrayOutputStream compress(ByteArrayOutputStream buffer)
    throws IOException {
    if (compressionBuffer == null) {
      compressionBuffer = new ByteArrayOutputStream(buffer.size());
    } else {
      compressionBuffer.reset();
    }
    if (compressor == null) {
      compressor = new JFastLZPack();
    }
    byte[] original = buffer.toByteArray();
    byte[] packed = new byte[original.length*6];

    // Pass output through fastlz, and prepend with length of compressed output.
    int packedSize = compressor.pack(original, 0, original.length, packed, 0,
        packed.length, JFastLZLevel.evaluateLevel(compressionLevel));
    // writeBytes will first write the length as a long
    compressionBuffer.write(packed, 0, packedSize);
    return compressionBuffer;
  }

  @Override
  Decoder decompress(byte[] in) throws IOException {
    if (decompressor == null) {
      decompressor = new JFastLZUnpack();
    }
    ByteArrayOutputStream uncompressed = new ByteArrayOutputStream(in.length*6);
    ByteArrayInputStream compressed = new ByteArrayInputStream(in);     

    decompressor.unpackStream(compressed, uncompressed);
    byte[] buffer = uncompressed.toByteArray();
    
    InputStream uncompressedInput = new ByteArrayInputStream(buffer);
    return new BinaryDecoder(uncompressedInput);
  }

}
