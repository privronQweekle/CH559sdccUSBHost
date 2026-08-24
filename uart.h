#ifndef __uart_H__
#define __uart_H__

#define MSG_TYPE_CONNECTED      0x01
#define MSG_TYPE_DISCONNECTED   0x02
#define MSG_TYPE_ERROR          0x03
#define MSG_TYPE_DEVICE_POLL    0x04
#define MSG_TYPE_DEVICE_STRING  0x05
#define MSG_TYPE_DEVICE_INFO    0x06
#define MSG_TYPE_HID_INFO       0x07
#define MSG_TYPE_STARTUP        0x08
#define MSG_TYPE_CDC_DATA       0x09
#define MSG_TYPE_VERSION        0x0A

// Protocol/capability version this firmware speaks. Bump PROTOCOL_VERSION
// whenever the UART frame format or command set changes in a way an ESP32-
// side consumer needs to know about (e.g. new baud rate support). Firmware
// that doesn't understand the version query (anything before this feature)
// never sends MSG_TYPE_VERSION at all — its absence within a timeout is
// itself the "no, I don't support this" signal on the ESP32 side.
#define PROTOCOL_VERSION_MAJOR  1
#define PROTOCOL_VERSION_MINOR  0
// Capability bit flags, sent as the MSG_TYPE_VERSION payload's 3rd byte.
// Originally targeted 460800, but real hardware testing found it has no
// achievable low-error Timer1 reload value at this chip's FREQ_SYS=48MHz
// (nearest integer reload lands at -7% baud error, well outside UART
// tolerance; 230400 lands at +0.16%, same as the already-reliable 115200).
#define CAP_BAUD_230400         0x01 // firmware can switch UART0 to 230400 on request

void processUart();
// Sends the version/capability reply if the last received command was a
// version query (see processUart()'s handling of the 0xFE 'V' query).
// No-op if no query is pending.
void maybeSendVersionReply();
// Applies a pending baud-rate switch requested via the 0xFE 'B' <rate> '\n'
// command (see processUart()), if any. Must be called from the main loop
// AFTER maybeSendVersionReply()/any other pending UART output for this
// iteration, since it reconfigures UART0's speed and anything queued
// before the switch but not yet physically transmitted would be corrupted
// by the speed change mid-byte.
void maybeApplyBaudSwitch();
void sendHidPollMSG(unsigned char msgtype, unsigned short length, unsigned char type, unsigned char device, unsigned char endpoint, unsigned char __xdata *msgbuffer,unsigned char idVendorL,unsigned char idVendorH,unsigned char idProductL,unsigned char idProductH);
void sendProtocolMSG(unsigned char msgtype, unsigned short length, unsigned char type, unsigned char device, unsigned char endpoint, unsigned char __xdata *msgbuffer);

#endif