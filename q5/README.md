# Embedded Firmware Log Compression Utility

A lossless data compression and decompression utility built for remote embedded monitoring devices with limited communication bandwidth.

## Features
- **Huffman Coding Algorithm:** Analyzes character frequencies, constructs optimal prefix code trees via min-heap priority queues, and encodes log files bit-by-bit.
- **Self-Contained Header Serialization:** Serializes unique character frequencies directly into the `.huff` container so the Huffman tree can be reconstructed dynamically during decompression.
- **Automated Verification:** Performs a strict byte-by-byte comparison between the original file (`device_log.txt`) and the restored file (`device_log_restored.txt`) to verify zero data loss.
- **Bandwidth Metrics:** Reports original size, compressed size, and compression ratio percentage.