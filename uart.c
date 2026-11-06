
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "CH559.h"
#include "util.h"
#include "uart.h"

uint8_t __xdata uartRxBuff[64];
uint8_t __xdata rxPos = 0;

// Set by processUart() when a version query (0xFE 'V' '\n') is received;
// consumed by maybeSendVersionReply(), which the main loop calls right
// after processUart() so the reply goes out promptly at the CURRENT baud
// rate (any baud switch a future command triggers must happen only after
// this reply is fully clocked out, never before).
static uint8_t __xdata versionQueryPending = 0;

// Set by processUart() when a baud-switch command (0xFE 'B' <rateCode> '\n')
// is received; 0 = no switch pending. Consumed by maybeApplyBaudSwitch().
static uint8_t __xdata pendingBaudRateCode = 0;

// Rate codes for the 'B' command, deliberately NOT the raw baud value so a
// malformed/garbage byte can't select an arbitrary (and possibly invalid)
// divisor — only values this firmware explicitly recognizes take effect.
#define BAUD_CODE_115200  0
#define BAUD_CODE_230400  1
#define BAUD_CODE_300000  2

void processUart(){
    uint8_t c;
    while(RI){
            RI=0;
            c = SBUF;
            // Line longer than the buffer: drop it whole, resync on the next
            // '\n'. The previous code stored the byte BEFORE checking the
            // bound, so the 65th byte of an unterminated line landed one past
            // the end of uartRxBuff, on whatever xdata the linker placed next.
            // In the 0a10266 build that was maybeSendVersionReply()'s scratch
            // payload[0] (harmless, rewritten before use), but it depended on
            // link order, and the truncated 64-byte chunk was then parsed as
            // a command. Line noise or bytes sent at the wrong baud rate (e.g.
            // a 300000 probe seen by a module still at 115200) produce exactly
            // such unterminated runs.
            if (rxPos >= sizeof(uartRxBuff)) {
                if (c == '\n') rxPos = 0;
                continue;
            }
            uartRxBuff[rxPos] = c;
            if (c == '\n'){
                // Version query: 0xFE 'V' '\n' (3 bytes incl. terminator).
                // Checked BEFORE the debug hex-dump/echo below so a query
                // never gets echoed back — a genuine ESP32-side consumer
                // only ever sees either silence (old firmware, which falls
                // through to the echo+'k'/'b' check exactly as before) or
                // a clean MSG_TYPE_VERSION frame (this firmware), never
                // both.
                if (rxPos == 2 && uartRxBuff[0] == 0xFE && uartRxBuff[1] == 'V') {
                    versionQueryPending = 1;
                    rxPos = 0;
                    continue;
                }
                // Baud switch: 0xFE 'B' <rateCode> '\n' (4 bytes incl.
                // terminator). Same rationale as the version query — must be
                // recognized before the debug echo so it's silently
                // swallowed rather than echoed and left for a confused
                // ESP32-side parser to deal with.
                if (rxPos == 3 && uartRxBuff[0] == 0xFE && uartRxBuff[1] == 'B') {
                    pendingBaudRateCode = uartRxBuff[2] + 1; // +1: 0 means "none pending"
                    rxPos = 0;
                    continue;
                }
                for (uint8_t i = 0; i < rxPos; i ++ )
                    {
                        printf( "0x%02X ",uartRxBuff[i]);
                    }
                    printf("\n");
                if(uartRxBuff[0]=='k'){
                //if(uartRxBuff[1]==0x61)LED=0;
                //if(uartRxBuff[1]==0x73)LED=1;
                if(uartRxBuff[1]=='b')runBootloader();
                }
            rxPos=0;
            }else{
            rxPos++;
            }
        }
}

void maybeSendVersionReply(){
    unsigned char __xdata payload[3];
    if (!versionQueryPending) return;
    versionQueryPending = 0;
    payload[0] = PROTOCOL_VERSION_MAJOR;
    payload[1] = PROTOCOL_VERSION_MINOR;
    payload[2] = CAP_BAUD_230400 | CAP_BAUD_300000;
    sendProtocolMSG(MSG_TYPE_VERSION, 3, 0, 0, 0, payload);
}

void maybeApplyBaudSwitch(){
    unsigned char __xdata rateCode;
    unsigned char __xdata ack[1];
    unsigned long __xdata newBaud;
    if (!pendingBaudRateCode) return;
    rateCode = pendingBaudRateCode - 1;
    pendingBaudRateCode = 0;

    switch (rateCode) {
        case BAUD_CODE_115200: newBaud = 115200; break;
        case BAUD_CODE_230400: newBaud = 230400; break;
        case BAUD_CODE_300000: newBaud = 300000; break;
        default: return; // unrecognized code: ignore, stay at current baud
    }

    // Ack at the CURRENT (pre-switch) baud so the ESP32 knows the command
    // was understood before it (independently) reconfigures its own side —
    // putchar() blocks until each byte is fully clocked out, so by the time
    // sendProtocolMSG() returns here the ack is physically on the wire.
    ack[0] = rateCode;
    sendProtocolMSG(MSG_TYPE_VERSION, 1, 1 /* type=1: this is a switch ack, not a capability reply */, 0, 0, ack);
    // Let the stop bit of the last ack byte finish, AND give margin against
    // the other side starting its next transmission (e.g. a 'v' query)
    // almost immediately after seeing the ack — a higher target baud has a
    // shorter bit period, so the same wall-clock margin buys less
    // protection there. 500us covers a full byte+idle gap at any supported
    // rate up to 300000 (~3.3us/bit) with plenty of room to spare.
    delayUs(500);
    initUART0(newBaud, 0); // alt=0: pins already configured at boot, only re-derive TH1/TL1
}

void sendProtocolMSG(unsigned char msgtype, unsigned short length, unsigned char type, unsigned char device, unsigned char endpoint, unsigned char __xdata *msgbuffer){
    unsigned short i;
    putchar(0xFE);	
	putchar(length);
	putchar((unsigned char)(length>>8));
	putchar(msgtype);
	putchar(type);
	putchar(device);
	putchar(endpoint);
	putchar(0);
	putchar(0);
	putchar(0);
	putchar(0);
	for (i = 0; i < length; i++)
	{
		putchar(msgbuffer[i]);
	}
	putchar('\n');
}

void sendHidPollMSG(unsigned char msgtype, unsigned short length, unsigned char type, unsigned char device, unsigned char endpoint, unsigned char __xdata *msgbuffer,unsigned char idVendorL,unsigned char idVendorH,unsigned char idProductL,unsigned char idProductH){
    unsigned short i;
    putchar(0xFE);	
	putchar(length);
	putchar((unsigned char)(length>>8));
	putchar(msgtype);
	putchar(type);
	putchar(device);
	putchar(endpoint);
	putchar(idVendorL);
	putchar(idVendorH);
	putchar(idProductL);
	putchar(idProductH);
	for (i = 0; i < length; i++)
	{
		putchar(msgbuffer[i]);
	}
	putchar('\n');
}