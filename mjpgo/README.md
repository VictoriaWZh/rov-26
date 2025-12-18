# mjpgo

A lean MJPEG streaming tool for Linux. Captures video from V4L2 devices and streams over UDP.

## Features

- **Capture**: Read MJPEG frames from V4L2 video devices
- **Send**: Transmit frames over UDP with automatic segmentation
- **Receive**: Reassemble UDP packets into complete frames
- **Profile**: Optional latency tracking with `--profile` flag

## Installation

### Dependencies

```bash
sudo apt-get update
sudo apt-get install build-essential v4l-utils
```

### Build

```bash
chmod +x build.sh
./build.sh
```

## Usage

```bash
mjpgo [--profile] [input] [outputs...]
```

### Commands

| Command | Description |
|---------|-------------|
| `help` | Display usage information |
| `devices` | List V4L2 devices with MJPEG support |

### Input Options

**capture** - Capture from a V4L2 device

| Argument | Type | Example | Description |
|----------|------|---------|-------------|
| DEVICE | string | `/dev/video0` | Device path |
| WIDTH | uint | `640` | Frame width |
| HEIGHT | uint | `480` | Frame height |
| FPS_NUM | uint | `1` | Framerate numerator |
| FPS_DEN | uint | `30` | Framerate denominator |

**receive** - Receive UDP stream

| Argument | Type | Example | Description |
|----------|------|---------|-------------|
| IP | string | `0.0.0.0` | Listen address |
| PORT | uint | `5001` | Listen port |
| PACKET_LEN | uint | `1400` | Max packet size |
| JPEG_LEN | uint | `500000` | Max frame size |
| WIDTH | uint | `640` | Frame width |
| HEIGHT | uint | `480` | Frame height |
| FPS_NUM | uint | `1` | Framerate numerator |
| FPS_DEN | uint | `30` | Framerate denominator |

### Output Options

**send** - Send UDP stream

| Argument | Type | Example | Description |
|----------|------|---------|-------------|
| LOCAL_IP | string | `0.0.0.0` | Source address |
| LOCAL_PORT | uint | `5000` | Source port |
| REMOTE_IP | string | `192.168.1.2` | Destination address |
| REMOTE_PORT | uint | `5001` | Destination port |
| PACKET_LEN | uint | `1400` | Max packet size |
| JPEG_LEN | uint | `500000` | Max frame size |
| ROUNDS | uint | `1` | Redundant sends per frame |

## Examples

### List Available Devices

```bash
./bin/mjpgo devices
```

### Capture and Send

```bash
./bin/mjpgo capture /dev/video0 640 480 1 30 \
    send 0.0.0.0 5000 192.168.1.2 5001 1400 500000 1
```

### Receive

```bash
./bin/mjpgo receive 0.0.0.0 5001 1400 500000 640 480 1 30
```

### Capture with Profiling

```bash
./bin/mjpgo --profile capture /dev/video0 640 480 1 30 \
    send 0.0.0.0 5000 192.168.1.2 5001 1400 500000 1
# Press Ctrl+C to see latency stats
```

### Chain: Receive and Forward

```bash
./bin/mjpgo receive 0.0.0.0 5001 1400 500000 640 480 1 30 \
    send 0.0.0.0 5002 192.168.1.3 5003 1400 500000 1
```

## Protocol

mjpgo uses a custom UDP protocol for transmitting JPEG frames:

### Packet Header (20 bytes)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 8 | `frame_ts_us` | Capture timestamp (microseconds, big-endian) |
| 8 | 4 | `seg_idx` | Segment index (big-endian) |
| 12 | 4 | `seg_count` | Total segments (big-endian) |
| 16 | 4 | `payload_len` | Payload bytes (big-endian) |

### Notes

- Large JPEG frames are split into multiple UDP packets
- Each packet contains a header + payload
- Receiver reassembles using segment index
- Timestamp enables latency measurement
- PACKET_LEN should fit within network MTU (typically 1400-1472 bytes)
- JPEG_LEN should exceed maximum expected frame size

## Profile Output

When using `--profile`, pressing Ctrl+C displays profiling statistics. e.g::

```
--- Profiling Statistics ---
Frames:     1000
Duration:   33.45 seconds
Average:    29.90 fps
Latency:
  Average:  50345 us
  Min:      45823 us
  Max:      70521 us