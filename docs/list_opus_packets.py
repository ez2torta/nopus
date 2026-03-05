
import struct
import sys

def parse_capcom_opus(filename):
    with open(filename, 'rb') as f:
        data = f.read()

    # Leer header Capcom (0x00-0x2F)
    if len(data) < 0x30:
        print('Archivo demasiado pequeño para ser Capcom OPUS')
        return
    num_samples = struct.unpack_from('<I', data, 0x00)[0]
    channels = struct.unpack_from('<I', data, 0x04)[0]
    loop_start = struct.unpack_from('<I', data, 0x08)[0]
    loop_end = struct.unpack_from('<I', data, 0x0C)[0]
    nintendo_off = struct.unpack_from('<I', data, 0x1C)[0]
    print(f"Num samples: {num_samples}")
    print(f"Channels: {channels}")
    print(f"Loop start: {loop_start}, Loop end: {loop_end}")
    print(f"Nintendo header offset: 0x{nintendo_off:X}")

    # Calcular paquetes esperados según samples y samples por paquete
    SAMPLES_PER_PACKET = 960  # Nintendo/Capcom OPUS estándar
    expected_packets = num_samples // SAMPLES_PER_PACKET
    print(f"[INFO] Paquetes esperados (por samples): {expected_packets}")

    # Leer subheader Nintendo OPUS
    n_off = nintendo_off
    if len(data) < n_off + 0x18:
        print('Archivo demasiado pequeño para Nintendo OPUS header')
        return
    chunk_id = struct.unpack_from('<I', data, n_off + 0x00)[0]
    chunk_size = struct.unpack_from('<I', data, n_off + 0x04)[0]
    version = data[n_off + 0x08]
    channel_count = data[n_off + 0x09]
    frame_size = struct.unpack_from('<H', data, n_off + 0x0A)[0]
    sample_rate = struct.unpack_from('<I', data, n_off + 0x0C)[0]
    print(f"Nintendo OPUS chunk ID: 0x{chunk_id:X}, chunk size: {chunk_size}")
    print(f"Version: {version}, Channel count: {channel_count}, Frame size: {frame_size}, Sample rate: {sample_rate}")

    # Buscar chunk de datos OPUS (0x80000004)
    data_off = n_off + 0x18
    found = False
    for i in range(n_off + 0x18, min(n_off + 0x100, len(data) - 4)):
        if struct.unpack_from('<I', data, i)[0] == 0x80000004:
            data_off = i - 0x08
            found = True
            break
    if not found:
        print('No se encontró el chunk de datos OPUS (0x80000004)')
        return

    data_size = struct.unpack_from('<I', data, data_off + 0x04)[0]
    print(f"Data chunk offset: 0x{data_off:X}, size: {data_size}")
    print("[DEBUG] Hexdump de data chunk (primeros 64 bytes después de header):")
    print(data[data_off+8:data_off+8+64].hex(' '))


    # Escaneo byte a byte desde data_off+4 para encontrar el primer paquete válido
    start = data_off + 4
    end = start + data_size
    found = False
    max_scan = min(128, end - start - 8)  # escaneo más amplio por seguridad
    for scan in range(0, max_scan):
        test_offset = start + scan
        packet_size = struct.unpack('>I', data[test_offset:test_offset+4])[0]
        # Consideramos razonable un paquete entre 10 y 1500 bytes
        if 10 <= packet_size <= 1500 and test_offset + 8 + packet_size <= end:
            # Chequeo extra: el siguiente paquete también debe ser razonable
            next_offset = test_offset + 8 + packet_size
            if next_offset + 8 <= end:
                next_packet_size = struct.unpack('>I', data[next_offset:next_offset+4])[0]
                if 10 <= next_packet_size <= 1500:
                    offset = test_offset
                    found = True
                    print(f"[DEBUG] Primer paquete razonable encontrado en offset 0x{offset:X} (scan {scan} bytes desde data_off+4)")
                    break
    if not found:
        print('[ERROR] No se encontró un inicio válido de paquetes Opus (escaneo byte a byte desde data_off+4)')
        return
    idx = 0
    real_packets = 0
    print(f"\n[DEBUG] offset inicio paquetes: 0x{offset:X}, offset final: 0x{end:X}, data_size: {data_size}")
    print("Paquetes Opus:")
    while offset + 8 <= end:
        try:
            packet_size = struct.unpack('>I', data[offset:offset+4])[0]
            final_range = struct.unpack('>I', data[offset+4:offset+8])[0]
        except Exception as e:
            print(f"[DEBUG] Excepción al leer paquete en offset 0x{offset:X}: {e}")
            break
        print(f"[DEBUG] offset=0x{offset:X}, packet_size={packet_size}, final_range=0x{final_range:08X}")
        if packet_size == 0:
            offset += 8
            idx += 1
            continue
        if offset + 8 + packet_size > end:
            print(f"[DEBUG] El paquete se sale del rango de datos: offset+8+packet_size=0x{offset+8+packet_size:X} > end=0x{end:X}")
            break
        print(f"  Paquete {idx}: size={packet_size}, final_range=0x{final_range:08X}")
        offset += 8 + packet_size
        idx += 1
        real_packets += 1
    print(f"Total paquetes: {real_packets}")
    if real_packets == expected_packets:
        print(f"[OK] El número de paquetes coincide con los samples y el estándar OPUS.")
    else:
        print(f"[WARN] Paquetes contados ({real_packets}) != esperados ({expected_packets})")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print('Uso: python list_opus_packets.py <archivo_opus_capcom>')
        sys.exit(1)
    parse_capcom_opus(sys.argv[1])
