import argparse
import time

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


FUNC_READ_HOLDING = 0x03
FUNC_READ_INPUT = 0x04
FUNC_WRITE_SINGLE = 0x06
FUNC_WRITE_MULTIPLE = 0x10

REG_SPEED_MODE = 0x00F6
REG_POSITION_MODE = 0x00FD
REG_STOP = 0x00FE
REG_REALTIME_SPEED = 0x0035
REG_REALTIME_POSITION = 0x0036
REG_HOME_MOTOR_FLAGS = 0x003C


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
            crc &= 0xFFFF
    return crc


def u16be(data: bytes, offset: int) -> int:
    return (data[offset] << 8) | data[offset + 1]


def put_u16be(value: int) -> bytes:
    return bytes(((value >> 8) & 0xFF, value & 0xFF))


def append_crc(frame: bytes) -> bytes:
    crc = crc16_modbus(frame)
    return frame + bytes((crc & 0xFF, (crc >> 8) & 0xFF))


def build_read_response(addr: int, func: int, regs: list[int]) -> bytes:
    payload = bytearray((addr, func, len(regs) * 2))
    for value in regs:
        payload.extend(put_u16be(value & 0xFFFF))
    return append_crc(bytes(payload))


def build_exception(addr: int, func: int, code: int) -> bytes:
    return append_crc(bytes((addr, func | 0x80, code & 0xFF)))


def try_parse_request(rx: bytearray):
    if len(rx) < 8:
        return None

    func = rx[1]
    if func in (FUNC_READ_HOLDING, FUNC_READ_INPUT, FUNC_WRITE_SINGLE):
        total = 8
    elif func == FUNC_WRITE_MULTIPLE:
        if len(rx) < 7:
            return None
        total = 9 + rx[6]
    else:
        total = 8

    if len(rx) < total:
        return None

    raw = bytes(rx[:total])
    del rx[:total]

    crc_recv = raw[-2] | (raw[-1] << 8)
    crc_calc = crc16_modbus(raw[:-2])
    if crc_recv != crc_calc:
        print(f"crc error recv=0x{crc_recv:04x} calc=0x{crc_calc:04x} raw={raw.hex(' ')}")
        return None

    return raw


def main() -> None:
    parser = argparse.ArgumentParser(description="Fake ZDT Emm ModbusRTU slave for Vision Board RS485 tests")
    parser.add_argument("--port", required=True, help="Serial port, for example COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--addr", type=lambda x: int(x, 0), default=2)
    parser.add_argument("--delay-ms", type=int, default=8)
    args = parser.parse_args()

    rx = bytearray()
    fake_speed = 0
    fake_position = 0x00001234
    home_flags = 0x03
    motor_flags = 0x03

    with serial.Serial(args.port, args.baud, timeout=0.02) as ser:
        print(f"fake zdt modbus slave port={args.port} baud={args.baud} addr={args.addr}")
        while True:
            data = ser.read(256)
            if data:
                rx.extend(data)

            req = try_parse_request(rx)
            if req is None:
                continue

            addr = req[0]
            func = req[1]
            if addr not in (args.addr, 0):
                print(f"ignore addr={addr} raw={req.hex(' ')}")
                continue

            resp = None
            if func in (FUNC_READ_HOLDING, FUNC_READ_INPUT):
                reg = u16be(req, 2)
                count = u16be(req, 4)
                if reg == REG_REALTIME_SPEED and count == 2:
                    sign = 1 if fake_speed < 0 else 0
                    regs = [(sign << 8), abs(fake_speed)]
                    resp = build_read_response(addr, func, regs)
                elif reg == REG_REALTIME_POSITION and count == 3:
                    sign = 0
                    regs = [(sign << 8), (fake_position >> 16) & 0xFFFF, fake_position & 0xFFFF]
                    resp = build_read_response(addr, func, regs)
                elif reg == REG_HOME_MOTOR_FLAGS and count == 1:
                    resp = build_read_response(addr, func, [(home_flags << 8) | motor_flags])
                else:
                    resp = build_exception(addr, func, 0x02)
                print(f"rx read addr={addr} func=0x{func:02x} reg=0x{reg:04x} count={count}")
            elif func == FUNC_WRITE_SINGLE:
                reg = u16be(req, 2)
                value = u16be(req, 4)
                if reg == REG_STOP:
                    fake_speed = 0
                    resp = req
                else:
                    resp = build_exception(addr, func, 0x02)
                print(f"rx write_single addr={addr} reg=0x{reg:04x} value=0x{value:04x}")
            elif func == FUNC_WRITE_MULTIPLE:
                reg = u16be(req, 2)
                count = u16be(req, 4)
                values = [u16be(req, 7 + i * 2) for i in range(count)]
                if reg == REG_SPEED_MODE and count == 3:
                    direction = (values[0] >> 8) & 0xFF
                    rpm = values[1]
                    fake_speed = -rpm if direction else rpm
                    resp = append_crc(req[:6])
                elif reg == REG_POSITION_MODE and count == 5:
                    fake_position = ((values[2] << 16) | values[3]) & 0xFFFFFFFF
                    resp = append_crc(req[:6])
                else:
                    resp = build_exception(addr, func, 0x02)
                print(f"rx write_multi addr={addr} reg=0x{reg:04x} count={count} values={[hex(v) for v in values]}")
            else:
                resp = build_exception(addr, func, 0x01)

            if addr == 0:
                continue

            time.sleep(max(args.delay_ms, 0) / 1000.0)
            ser.write(resp)
            ser.flush()
            print(f"tx {resp.hex(' ')}")


if __name__ == "__main__":
    main()
