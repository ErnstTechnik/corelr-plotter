#!/usr/bin/env python3
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Minimal cycle-counting ATmega328P interpreter, for measuring pure CPU code.

Scope: executes an ELF's flash image (avr-objcopy -O binary) with the
datasheet cycle table. No peripherals, no interrupts. Three "trap"
registers stand in for I/O:
  GPIOR0 (data 0x3E)  write -> append byte to the event log
  GPIOR1 (data 0x4A)  write -> halt
  GPIOR2 (data 0x4B)  write -> record (cycle count, value) mark
Validated by the caller against a host build of the same source.
"""
import sys

SRAM_END = 0x8FF
SREG, SPL, SPH = 0x5F, 0x5D, 0x5E
C, Z, N, V, S, H, T, I = 0, 1, 2, 3, 4, 5, 6, 7


class Halt(Exception):
    pass


class AVR:
    def __init__(self, flash_bytes):
        self.flash = bytearray(flash_bytes) + bytearray(2 * 16384 - len(flash_bytes))
        self.w = [self.flash[2 * i] | (self.flash[2 * i + 1] << 8) for i in range(16384)]
        self.mem = bytearray(0x900)
        self.pc = 0
        self.cycles = 0
        self.log = []
        self.marks = []
        self.mem[SPL] = SRAM_END & 0xFF
        self.mem[SPH] = SRAM_END >> 8
        self.icount = 0

    # ---- helpers -------------------------------------------------
    def sreg(self, bit):
        return (self.mem[SREG] >> bit) & 1

    def setf(self, bit, val):
        if val:
            self.mem[SREG] |= (1 << bit)
        else:
            self.mem[SREG] &= ~(1 << bit) & 0xFF

    def rd(self, a):
        return self.mem[a]

    def wr(self, a, v):
        v &= 0xFF
        if a == 0x3E:
            self.log.append(v)
        elif a == 0x4A:
            raise Halt()
        elif a == 0x4B:
            self.marks.append((self.cycles, v))
        self.mem[a] = v

    def push(self, v):
        sp = self.mem[SPL] | (self.mem[SPH] << 8)
        self.mem[sp] = v & 0xFF
        sp -= 1
        self.mem[SPL] = sp & 0xFF
        self.mem[SPH] = (sp >> 8) & 0xFF

    def pop(self):
        sp = (self.mem[SPL] | (self.mem[SPH] << 8)) + 1
        self.mem[SPL] = sp & 0xFF
        self.mem[SPH] = (sp >> 8) & 0xFF
        return self.mem[sp]

    def pair(self, lo):
        return self.mem[lo] | (self.mem[lo + 1] << 8)

    def setpair(self, lo, v):
        self.mem[lo] = v & 0xFF
        self.mem[lo + 1] = (v >> 8) & 0xFF

    def flags_add(self, a, b, r8, carry_in=0):
        r = r8 & 0xFF
        self.setf(H, ((a & 0xF) + (b & 0xF) + carry_in) > 0xF)
        self.setf(C, (a + b + carry_in) > 0xFF)
        self.setf(V, (~(a ^ b) & (a ^ r) & 0x80) != 0)
        self.setf(N, r & 0x80)
        self.setf(S, self.sreg(N) ^ self.sreg(V))
        return r

    def flags_sub(self, a, b, carry_in=0, keepz=False):
        res = a - b - carry_in
        r = res & 0xFF
        self.setf(H, (a & 0xF) < ((b & 0xF) + carry_in))
        self.setf(C, res < 0)
        self.setf(V, ((a ^ b) & (a ^ r) & 0x80) != 0)
        self.setf(N, r & 0x80)
        self.setf(S, self.sreg(N) ^ self.sreg(V))
        if keepz:
            if r != 0:
                self.setf(Z, 0)
        else:
            self.setf(Z, r == 0)
        return r

    def flags_logic(self, r):
        self.setf(V, 0)
        self.setf(N, r & 0x80)
        self.setf(S, self.sreg(N))
        self.setf(Z, r == 0)

    def is32(self, w):
        # two-word instructions: LDS, STS, JMP, CALL
        return (w & 0xFE0E) in (0x940C, 0x940E) or (w & 0xFC0F) in (0x9000, 0x9200)

    def skip(self):
        nxt = self.w[self.pc]
        if self.is32(nxt):
            self.pc += 2
            return 3
        self.pc += 1
        return 2

    # ---- one instruction -----------------------------------------
    def step(self):
        w = self.w[self.pc]
        self.pc += 1
        self.icount += 1
        m = self.mem
        rd5 = (w >> 4) & 0x1F
        rr5 = ((w >> 5) & 0x10) | (w & 0xF)
        k8 = ((w >> 4) & 0xF0) | (w & 0xF)
        rd4 = 16 + ((w >> 4) & 0xF)

        if w == 0x0000:
            self.cycles += 1
            return
        top = w >> 10
        # --- two-register ALU -----------------------------------
        if (w & 0xFC00) == 0x0C00:  # ADD
            a, b = m[rd5], m[rr5]
            m[rd5] = self.flags_add(a, b, a + b)
            self.setf(Z, m[rd5] == 0)
            self.setf(S, self.sreg(N) ^ self.sreg(V))
            self.cycles += 1
            return
        if (w & 0xFC00) == 0x1C00:  # ADC
            a, b, c = m[rd5], m[rr5], self.sreg(C)
            m[rd5] = self.flags_add(a, b, a + b + c, c)
            self.setf(Z, m[rd5] == 0)
            self.cycles += 1
            return
        if (w & 0xFC00) == 0x1800:  # SUB
            m[rd5] = self.flags_sub(m[rd5], m[rr5])
            self.cycles += 1
            return
        if (w & 0xFC00) == 0x0800:  # SBC
            m[rd5] = self.flags_sub(m[rd5], m[rr5], self.sreg(C), keepz=True)
            self.cycles += 1
            return
        if (w & 0xFC00) == 0x1400:  # CP
            self.flags_sub(m[rd5], m[rr5])
            self.cycles += 1
            return
        if (w & 0xFC00) == 0x0400:  # CPC
            self.flags_sub(m[rd5], m[rr5], self.sreg(C), keepz=True)
            self.cycles += 1
            return
        if (w & 0xFC00) == 0x1000:  # CPSE
            self.cycles += 1
            if m[rd5] == m[rr5]:
                self.cycles += self.skip() - 1
            return
        if (w & 0xFC00) == 0x2000:  # AND
            m[rd5] &= m[rr5]
            self.flags_logic(m[rd5])
            self.cycles += 1
            return
        if (w & 0xFC00) == 0x2400:  # EOR
            m[rd5] ^= m[rr5]
            self.flags_logic(m[rd5])
            self.cycles += 1
            return
        if (w & 0xFC00) == 0x2800:  # OR
            m[rd5] |= m[rr5]
            self.flags_logic(m[rd5])
            self.cycles += 1
            return
        if (w & 0xFC00) == 0x2C00:  # MOV
            m[rd5] = m[rr5]
            self.cycles += 1
            return
        if (w & 0xFF00) == 0x0100:  # MOVW
            d = ((w >> 4) & 0xF) * 2
            r = (w & 0xF) * 2
            m[d], m[d + 1] = m[r], m[r + 1]
            self.cycles += 1
            return
        if (w & 0xFF00) == 0x0200:  # MULS
            a = m[16 + ((w >> 4) & 0xF)]
            b = m[16 + (w & 0xF)]
            a = a - 256 if a & 0x80 else a
            b = b - 256 if b & 0x80 else b
            p = (a * b) & 0xFFFF
            self.setpair(0, p)
            self.setf(C, p & 0x8000)
            self.setf(Z, p == 0)
            self.cycles += 2
            return
        if (w & 0xFF88) == 0x0300:  # MULSU
            a = m[16 + ((w >> 4) & 7)]
            b = m[16 + (w & 7)]
            a = a - 256 if a & 0x80 else a
            p = (a * b) & 0xFFFF
            self.setpair(0, p)
            self.setf(C, p & 0x8000)
            self.setf(Z, p == 0)
            self.cycles += 2
            return
        if (w & 0xFC00) == 0x9C00:  # MUL
            p = m[rd5] * m[rr5]
            self.setpair(0, p)
            self.setf(C, p & 0x8000)
            self.setf(Z, (p & 0xFFFF) == 0)
            self.cycles += 2
            return
        # --- register + immediate -------------------------------
        t = w & 0xF000
        if t == 0x3000:  # CPI
            self.flags_sub(m[rd4], k8)
            self.cycles += 1
            return
        if t == 0x4000:  # SBCI
            m[rd4] = self.flags_sub(m[rd4], k8, self.sreg(C), keepz=True)
            self.cycles += 1
            return
        if t == 0x5000:  # SUBI
            m[rd4] = self.flags_sub(m[rd4], k8)
            self.cycles += 1
            return
        if t == 0x6000:  # ORI
            m[rd4] |= k8
            self.flags_logic(m[rd4])
            self.cycles += 1
            return
        if t == 0x7000:  # ANDI
            m[rd4] &= k8
            self.flags_logic(m[rd4])
            self.cycles += 1
            return
        if t == 0xE000:  # LDI
            m[rd4] = k8
            self.cycles += 1
            return
        # --- ADIW / SBIW ----------------------------------------
        if (w & 0xFF00) in (0x9600, 0x9700):
            d = 24 + 2 * ((w >> 4) & 3)
            k = ((w >> 2) & 0x30) | (w & 0xF)
            v = self.pair(d)
            if (w & 0xFF00) == 0x9600:
                r = (v + k) & 0xFFFF
                self.setf(V, (~v & r) & 0x8000)
                self.setf(C, (~r & v) & 0x8000)
            else:
                r = (v - k) & 0xFFFF
                self.setf(V, (v & ~r) & 0x8000)
                self.setf(C, (r & ~v) & 0x8000)
            self.setf(N, r & 0x8000)
            self.setf(Z, r == 0)
            self.setf(S, self.sreg(N) ^ self.sreg(V))
            self.setpair(d, r)
            self.cycles += 2
            return
        # --- single register ------------------------------------
        if (w & 0xFE00) == 0x9400:
            op = w & 0xF
            if op == 0x0:  # COM
                m[rd5] = (~m[rd5]) & 0xFF
                self.flags_logic(m[rd5])
                self.setf(C, 1)
                self.cycles += 1
                return
            if op == 0x1:  # NEG
                a = m[rd5]
                r = (-a) & 0xFF
                self.setf(H, (r | a) & 0x08)
                self.setf(V, r == 0x80)
                self.setf(C, r != 0)
                self.setf(N, r & 0x80)
                self.setf(S, self.sreg(N) ^ self.sreg(V))
                self.setf(Z, r == 0)
                m[rd5] = r
                self.cycles += 1
                return
            if op == 0x2:  # SWAP
                m[rd5] = ((m[rd5] << 4) | (m[rd5] >> 4)) & 0xFF
                self.cycles += 1
                return
            if op == 0x3:  # INC
                r = (m[rd5] + 1) & 0xFF
                self.setf(V, r == 0x80)
                self.setf(N, r & 0x80)
                self.setf(S, self.sreg(N) ^ self.sreg(V))
                self.setf(Z, r == 0)
                m[rd5] = r
                self.cycles += 1
                return
            if op in (0x5, 0x6, 0x7):  # ASR, LSR, ROR
                a = m[rd5]
                if op == 0x5:
                    r = (a >> 1) | (a & 0x80)
                elif op == 0x6:
                    r = a >> 1
                else:
                    r = (a >> 1) | (self.sreg(C) << 7)
                self.setf(C, a & 1)
                self.setf(N, r & 0x80)
                self.setf(V, self.sreg(N) ^ self.sreg(C))
                self.setf(S, self.sreg(N) ^ self.sreg(V))
                self.setf(Z, r == 0)
                m[rd5] = r
                self.cycles += 1
                return
            if op == 0xA:  # DEC
                r = (m[rd5] - 1) & 0xFF
                self.setf(V, r == 0x7F)
                self.setf(N, r & 0x80)
                self.setf(S, self.sreg(N) ^ self.sreg(V))
                self.setf(Z, r == 0)
                m[rd5] = r
                self.cycles += 1
                return
        # --- SREG bit ops, jumps, calls -------------------------
        if (w & 0xFF8F) == 0x9408:  # BSET
            self.setf((w >> 4) & 7, 1)
            self.cycles += 1
            return
        if (w & 0xFF8F) == 0x9488:  # BCLR
            self.setf((w >> 4) & 7, 0)
            self.cycles += 1
            return
        if w == 0x9508:  # RET
            hi = self.pop()
            lo = self.pop()
            self.pc = (hi << 8) | lo
            self.cycles += 4
            return
        if w == 0x9518:  # RETI
            hi = self.pop()
            lo = self.pop()
            self.pc = (hi << 8) | lo
            self.setf(I, 1)
            self.cycles += 4
            return
        if w == 0x9409:  # IJMP
            self.pc = self.pair(30)
            self.cycles += 2
            return
        if w == 0x9509:  # ICALL
            ret = self.pc
            self.push(ret & 0xFF)
            self.push(ret >> 8)
            self.pc = self.pair(30)
            self.cycles += 3
            return
        if (w & 0xFE0E) == 0x940C:  # JMP
            self.pc = self.w[self.pc]
            self.cycles += 3
            return
        if (w & 0xFE0E) == 0x940E:  # CALL
            tgt = self.w[self.pc]
            ret = self.pc + 1
            self.push(ret & 0xFF)
            self.push(ret >> 8)
            self.pc = tgt
            self.cycles += 4
            return
        if (w & 0xF000) == 0xC000:  # RJMP
            k = w & 0xFFF
            if k & 0x800:
                k -= 0x1000
            self.pc = (self.pc + k) & 0x3FFF
            self.cycles += 2
            return
        if (w & 0xF000) == 0xD000:  # RCALL
            k = w & 0xFFF
            if k & 0x800:
                k -= 0x1000
            ret = self.pc
            self.push(ret & 0xFF)
            self.push(ret >> 8)
            self.pc = (self.pc + k) & 0x3FFF
            self.cycles += 3
            return
        if (w & 0xFC00) in (0xF000, 0xF400):  # BRBS / BRBC
            k = (w >> 3) & 0x7F
            if k & 0x40:
                k -= 0x80
            bit = w & 7
            take = self.sreg(bit) == (0 if (w & 0x400) else 1)
            if take:
                self.pc = (self.pc + k) & 0x3FFF
                self.cycles += 2
            else:
                self.cycles += 1
            return
        if (w & 0xFE08) == 0xFC00 or (w & 0xFE08) == 0xFE00:  # SBRC / SBRS
            b = w & 7
            bit = (m[rd5] >> b) & 1
            want = 1 if (w & 0x200) else 0
            self.cycles += 1
            if bit == want:
                self.cycles += self.skip() - 1
            return
        if (w & 0xFE08) == 0xF800:  # BLD
            b = w & 7
            if self.sreg(T):
                m[rd5] |= (1 << b)
            else:
                m[rd5] &= ~(1 << b) & 0xFF
            self.cycles += 1
            return
        if (w & 0xFE08) == 0xFA00:  # BST
            self.setf(T, (m[rd5] >> (w & 7)) & 1)
            self.cycles += 1
            return
        # --- IO -------------------------------------------------
        if (w & 0xF800) == 0xB000:  # IN
            a = ((w >> 5) & 0x30) | (w & 0xF)
            m[rd5] = m[a + 0x20]
            self.cycles += 1
            return
        if (w & 0xF800) == 0xB800:  # OUT
            a = ((w >> 5) & 0x30) | (w & 0xF)
            self.wr(a + 0x20, m[rd5])
            self.cycles += 1
            return
        if (w & 0xFF00) in (0x9A00, 0x9800):  # SBI / CBI
            a = ((w >> 3) & 0x1F) + 0x20
            b = w & 7
            v = m[a]
            v = (v | (1 << b)) if (w & 0xFF00) == 0x9A00 else (v & ~(1 << b) & 0xFF)
            self.wr(a, v)
            self.cycles += 2
            return
        if (w & 0xFF00) in (0x9B00, 0x9900):  # SBIS / SBIC
            a = ((w >> 3) & 0x1F) + 0x20
            bit = (m[a] >> (w & 7)) & 1
            want = 1 if (w & 0xFF00) == 0x9B00 else 0
            self.cycles += 1
            if bit == want:
                self.cycles += self.skip() - 1
            return
        # --- LPM ------------------------------------------------
        if w == 0x95C8:  # LPM r0,Z
            z = self.pair(30)
            m[0] = self.flash[z]
            self.cycles += 3
            return
        if (w & 0xFE0E) == 0x9004:  # LPM Rd,Z / Z+
            z = self.pair(30)
            m[rd5] = self.flash[z]
            if w & 1:
                self.setpair(30, (z + 1) & 0xFFFF)
            self.cycles += 3
            return
        # --- memory ---------------------------------------------
        if (w & 0xFE0F) == 0x9000:  # LDS
            a = self.w[self.pc]
            self.pc += 1
            m[rd5] = m[a]
            self.cycles += 2
            return
        if (w & 0xFE0F) == 0x9200:  # STS
            a = self.w[self.pc]
            self.pc += 1
            self.wr(a, m[rd5])
            self.cycles += 2
            return
        if (w & 0xFC00) == 0x9000 or (w & 0xFC00) == 0x9200:
            # LD/ST with X/Y/Z, +/-, and PUSH/POP
            store = (w & 0x0200) != 0
            op = w & 0xF
            if op == 0xF:
                if store:
                    self.push(m[rd5])
                else:
                    m[rd5] = self.pop()
                self.cycles += 2
                return
            if op in (0x4, 0x5, 0x6, 0x7, 0xA, 0xB, 0x8):
                raise NotImplementedError("LPM/XCH-class op %04x at %x" % (w, self.pc - 1))
            if op in (0xC, 0xD, 0xE):
                lo = 26
            elif op in (0x9, 0xA):
                lo = 28
            elif op in (0x1, 0x2, 0x0):
                lo = 30
            else:
                raise NotImplementedError("op %04x" % w)
            # Y: 1001 = Y+, 1010 = -Y ; Z: 0001 = Z+, 0010 = -Z ; X: C plain, D +, E -
            p = self.pair(lo)
            if op in (0xE, 0xA, 0x2):
                p = (p - 1) & 0xFFFF
                self.setpair(lo, p)
            if store:
                self.wr(p, m[rd5])
            else:
                m[rd5] = m[p]
            if op in (0xD, 0x9, 0x1):
                self.setpair(lo, (p + 1) & 0xFFFF)
            self.cycles += 2
            return
        if (w & 0xD000) == 0x8000:  # LDD / STD with displacement (Y, Z), incl. plain LD Y / LD Z
            q = ((w >> 8) & 0x20) | ((w >> 7) & 0x18) | (w & 7)
            lo = 28 if (w & 0x8) else 30
            store = (w & 0x0200) != 0
            a = (self.pair(lo) + q) & 0xFFFF
            if store:
                self.wr(a, m[rd5])
            else:
                m[rd5] = m[a]
            self.cycles += 2
            return
        if w in (0x95A8, 0x9598, 0x9588):  # WDR / BREAK / SLEEP
            self.cycles += 1
            return
        raise NotImplementedError("opcode %04x at word %x" % (w, self.pc - 1))

    def run(self, limit=2_000_000_000):
        try:
            while self.cycles < limit:
                self.step()
        except Halt:
            return
        raise RuntimeError("cycle limit")


def load(path):
    return open(path, "rb").read()


if __name__ == "__main__":
    import argparse
    _p = argparse.ArgumentParser(description="avrsim.py -- the cycle-counting ATmega328P interpreter (see README.md). Run as a module "
        "it executes a raw .bin until the program writes 1 to GPIOR1, and prints the cycles, the instruction count and the GPIOR0 log. "
        "The other tools here (run_*.py, profile.py) import it.")
    _p.add_argument("bin", metavar="BIN", help="a raw binary (avr-objcopy -O binary -j .text -j .data), e.g. out/t_motion.bin")
    _a = _p.parse_args()
    cpu = AVR(load(_a.bin))
    cpu.run()
    print("cycles", cpu.cycles, "insns", cpu.icount, "log", bytes(cpu.log))
