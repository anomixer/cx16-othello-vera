; =============================================================================
; pcmfeed.s - VERA PCM FIFO feed loop (Apple II, 1 MHz)
;
; The PCM stream is time-critical: the hardware drains the FIFO at 8,010.86 Hz
; whatever the game frame rate is, so every byte costs cycles that compete with
; the frame budget.  A C loop costs ~120 cycles/byte on this target because the
; 16-bit cursor and byte count live in memory across the loop; this version is
; 15 cycles/byte (lda abs,y / sta abs / iny / dex / bne).
;
; The source buffers are 512-byte block aligned ($0C00 and an aligned .bss
; buffer) and a call never feeds more than 128 bytes, so the absolute,Y offset
; never crosses a page and Y always starts at 0.  Both the source operand and
; the destination port are patched per call, so the same routine serves the
; slot-2 ($C21D) and slot-4 ($C41D) builds.
;
; C API (state below is exported and owned here):
;     extern uint8_t  pcm_src[2];      next byte, little-endian address
;     extern uint8_t  pcm_port[2];     VERA audio data port ($C21D / $C41D)
;     extern uint16_t pcm_remaining;   bytes left in the staged block
;     extern uint8_t  pcm_n;           bytes written by the last call
;     void pcm_feed_asm(void);         writes min(pcm_remaining, 128) bytes
;
; The FIFO level is judged by the caller through ISR bit 3 (audio FIFO almost
; empty = fewer than 1,024 bytes queued), which the real card and AppleWin both
; report as a live level; that keeps the stream self-regulating without relying
; on the FIFO-full bit, which AppleWin never sets (it compares
; m_fifo_cnt == FIFO_SIZE - 1, while writes cap the count one byte lower).
; =============================================================================

PCM_CHUNK = 128

        .section .bss
        .global pcm_src
        .global pcm_remaining
        .global pcm_n
pcm_src:        .byte 0, 0
pcm_remaining:  .byte 0, 0
pcm_n:          .byte 0

        .section .data
        .global pcm_port
pcm_port:       .byte 0x1d, 0xc2        ; slot 2 default; C overwrites for slot 4

        .section .text
        .global pcm_feed_asm

pcm_feed_asm:
        lda pcm_remaining+1
        bne .whole                ; >= 256 staged: feed one full chunk
        lda pcm_remaining
        beq .out                  ; nothing staged
        cmp #PCM_CHUNK+1
        bcc .counted
.whole: lda #PCM_CHUNK
.counted:
        sta pcm_n
        ldx pcm_n

        ; pcm_remaining -= n
        lda pcm_remaining
        sec
        sbc pcm_n
        sta pcm_remaining
        lda pcm_remaining+1
        sbc #0
        sta pcm_remaining+1

        ; patch the copy-loop operand and the destination port
        lda pcm_src
        sta .copy+1
        lda pcm_src+1
        sta .copy+2
        lda pcm_port
        sta .stor+1
        lda pcm_port+1
        sta .stor+2

        ; pcm_src += n
        lda pcm_src
        clc
        adc pcm_n
        sta pcm_src
        lda pcm_src+1
        adc #0
        sta pcm_src+1

        ldy #0
.copy:  lda $0C00,y              ; patched: current source address
.stor:  sta $C21D                ; patched: VERA PCM data port
        iny
        dex
        bne .copy
.out:   rts
