        .org $7FFF

STR_SIZE = $6

        LDX #$ff
Loop:
        INX
        CMX #STR_SIZE
        BIE End
        LDA Data,x
        INT #$10
        JMP Loop
End:    
        INT #%1

Data:
        .incbin "data.txt"
