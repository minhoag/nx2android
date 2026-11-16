import hashlib
import struct

SOURCE_SHA256 = 'c63ecc1321bfc91fd9e625b912333e8fa1268ee6f40f5e07bf6743525a8e08fa'
PATCHED_SHA256 = '5f1d0888f90debe846d7f4baa9572e3a92c354def602ee91637bd74ad7510c8b'
NEEDLE = bytes.fromhex('c40003018b0001078e0001080b010109')
CODE_OFFSET = 11545754


def held_item_fix(data):
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise ValueError('Scarlet held-item fix: unexpected main.blua source')
    order = list(range(40)) + list(range(44, 52)) + list(range(40, 44)) + list(range(52, 80))
    position = {old: new for new, old in enumerate(order)}
    position[80] = 80
    code = struct.unpack_from('<80I', data, CODE_OFFSET)
    reordered = []
    for new_pc, old_pc in enumerate(order):
        instruction = code[old_pc]
        if instruction & 127 == 56:
            destination = old_pc + 1 + (instruction >> 7) - 16777215
            jump = position[destination] - new_pc - 1 + 16777215
            if not 0 <= jump < 1 << 25:
                raise ValueError('Scarlet held-item fix: adjusted Lua jump is out of range')
            instruction = 56 | (jump << 7)
        reordered.append(instruction)
    patched = data[:CODE_OFFSET] + struct.pack('<80I', *reordered) + data[CODE_OFFSET + 320:]
    if hashlib.sha256(patched).hexdigest() != PATCHED_SHA256:
        raise ValueError('Scarlet held-item fix: patched output does not match the verified result')
    return patched
