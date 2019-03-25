        .org $7FFF

GPU_CTRL    = $0901
VBLANK      = $0902
CONTROLLER0 = $0903
PALETTE_ST  = $0907
PALETTE_DT  = $0908
SPRTEX_P    = $0909
BKGTEX_P    = $090B
BKG_PAL_MAP = $2E98
BKG_TEX_MAP = $3000
BKG_PALETTE = $33C0
SPR_PALETTE = $33E0
SCROLL_X    = $3400
SCROLL_Y    = $3401

Reset:
        lda VBLANK
        bie Reset  ; wait for vblank

        lda #%01100000 ; init gpu
        sta GPU_CTRL

        ; init background texture
        lda <BGTEX
        sta BKGTEX_P
        lda >BGTEX
        sta BKGTEX_P

        ; init sprite texture
        lda <SPRTEX
        sta SPRTEX_P
        lda >SPRTEX
        sta SPRTEX_P

        ; init sprites
        lda #50
        sta $0300 ; x
        lda #50
        sta $0301 ; y
        lda #%00000001
        sta $0302 ; attribute
        lda #0
        sta $0303 ; texture id

        ; init sprite palette
        lda <SPR_PALETTE
        sta PALETTE_ST
        lda >SPR_PALETTE
        sta PALETTE_ST

        ldx #$00
LoadSpPal:
        lda ALLPAL,x
        sta PALETTE_DT
        inx
        cmx #32
        bne LoadSpPal

        ; init background palette
        lda <BKG_PALETTE
        sta PALETTE_ST
        lda >BKG_PALETTE
        sta PALETTE_ST

        ldx #$00
LoadBgPal:
        lda ALLPAL,x
        sta PALETTE_DT
        inx
        cmx #32
        bne LoadBgPal

        ; init background map
        lda <BKG_TEX_MAP
        sta PALETTE_ST
        lda >BKG_TEX_MAP
        sta PALETTE_ST

        ldx #$00
LoadBgMap:
        lda BGMAP,x
        sta PALETTE_DT
        inx
        cmx #64
        bne LoadBgMap

        jmp End

Start:
        lda VBLANK
        bie Start  ; wait for vblank

        ; controls
        lda CONTROLLER0
        and #%10
        bie PadLeft

        ; flip player
        lda $0302
        and #%11111101
        sta $0302
        ; scroll
        lda SCROLL_X
        dea
        sta SCROLL_X

PadLeft:
        lda CONTROLLER0
        and #%100
        bie End

        ; flip player
        lda $0302
        aor #%00000010
        sta $0302
        ; scroll
        lda SCROLL_X
        ina
        sta SCROLL_X

End:
        lda VBLANK
        bie Start
        jmp End    ; wait for the end of vblank

ALLPAL:
        .incbin "data/all.pal"
BGMAP: 
        .incbin "data/bkg.map"
BGTEX:
        .incbin "data/bkg.tex"
SPRTEX:
        .incbin "data/spr.tex"
