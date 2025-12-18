# mjpgo

A lean MJPEG streaming tool for Linux. Captures video from V4L2 devices and streams over UDP.

## Features

- **Capture**: Read MJPEG frames from V4L2 video devices
- **Send**: Transmit frames over UDP with automatic segmentation
- **Receive**: Reassemble UDP packets into complete frames
- **Render**: Display video in SDL2 window with hardware acceleration
- **Record**: Save MJPEG stream to MKV file
- **Pipe**: Write JPEG frames to file descriptor
- **Profile**: Optional latency tracking with `--profile` flag

## Installation

### Dependencies

```bash
sudo apt-get update
sudo apt-get install build-essential v4l-utils libturbojpeg0-dev \
    libavformat-dev libavcodec-dev libavutil-dev libsdl2-dev
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

**render** - Display in SDL2 window

| Argument | Type | Example | Description |
|----------|------|---------|-------------|
| WINDOW_WIDTH | uint | `1280` | Window width |
| WINDOW_HEIGHT | uint | `720` | Window height |

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

**record** - Record to MKV file

| Argument | Type | Example | Description |
|----------|------|---------|-------------|
| FILENAME | string | `output.mkv` | Output file path |

**pipe** - Write JPEG frames to file descriptor

| Argument | Type | Example | Description |
|----------|------|---------|-------------|
| FD | int | `3` | File descriptor number |
| CHUNK_SIZE | uint | `4096` | Max bytes per write |

## Examples

### Display Camera Feed

```bash
./bin/mjpgo capture /dev/video0 640 480 1 30 render 1280 720
```

### Send and Display Locally

```bash
./bin/mjpgo capture /dev/video0 640 480 1 30 \
    render 640 480 \
    send 0.0.0.0 5000 192.168.1.2 5001 1400 500000 1
```

### Receive and Display

```bash
./bin/mjpgo receive 0.0.0.0 5001 1400 500000 640 480 1 30 render 1280 720
```

### Record to File

```bash
./bin/mjpgo capture /dev/video0 640 480 1 30 record output.mkv
```

### Multiple Outputs

```bash
./bin/mjpgo capture /dev/video0 640 480 1 30 \
    render 640 480 \
    send 0.0.0.0 5000 192.168.1.2 5001 1400 500000 1 \
    record backup.mkv
```

### Profiling

```bash
./bin/mjpgo --profile capture /dev/video0 640 480 1 30 render 640 480
# Close window or press Ctrl+C to see latency stats
```

## Render Controls

- **ESC** or **X button**: Close window and stop pipeline
- **Window resize**: Video scales to fit

## Pipe Protocol

JPEG frames are written to the pipe with this header:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 8 | `timestamp_us` | Capture timestamp (microseconds, big-endian) |
| 8 | 4 | `data_len` | JPEG data length (big-endian) |
| 12 | N | `data` | JPEG frame data |

## UDP Protocol

Packets use a 20-byte header:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 8 | `frame_ts_us` | Capture timestamp (big-endian) |
| 8 | 4 | `seg_idx` | Segment index (big-endian) |
| 12 | 4 | `seg_count` | Total segments (big-endian) |
| 16 | 4 | `payload_len` | Payload bytes (big-endian) |

## Profile Output

When using `--profile`, closing the window or pressing Ctrl+C displays:

```
--- Profiling Statistics ---
Frames:     1000
Duration:   33.45 seconds
Average:    29.90 fps
Latency:
  Average:  3245 us
  Min:      2823 us
  Max:      5521 us
```