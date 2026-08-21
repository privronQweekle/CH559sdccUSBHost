#include "CH559.h"
#include "USBHost.h"
#include "util.h"
#include "uart.h"
#include <string.h>

SBIT(LED, 0x90, 6);

typedef const unsigned char __code *PUINT8C;

__code unsigned char GetDeviceDescriptorRequest[] = 		{USB_REQ_TYP_IN, USB_GET_DESCRIPTOR, 0, USB_DESCR_TYP_DEVICE , 0, 0, sizeof(USB_DEV_DESCR), 0};
__code unsigned char GetConfigurationDescriptorRequest[] = 	{USB_REQ_TYP_IN, USB_GET_DESCRIPTOR, 0, USB_DESCR_TYP_CONFIG, 0, 0, sizeof(USB_DEV_DESCR), 0};
__code unsigned char GetInterfaceDescriptorRequest[] = 		{USB_REQ_TYP_IN | USB_REQ_RECIP_INTERF, USB_GET_DESCRIPTOR, 0, USB_DESCR_TYP_INTERF, 0, 0, sizeof(USB_ITF_DESCR), 0};
__code unsigned char SetUSBAddressRequest[] = 				{USB_REQ_TYP_OUT, USB_SET_ADDRESS, USB_DEVICE_ADDR, 0, 0, 0, 0, 0};
__code unsigned char GetDeviceStringRequest[] = 			{USB_REQ_TYP_IN, USB_GET_DESCRIPTOR, 2, 3, 9, 4, 2, 4};	//todo change language
__code unsigned char SetupSetUsbConfig[] = { USB_REQ_TYP_OUT, USB_SET_CONFIGURATION, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

__code unsigned char  SetHIDIdleRequest[] = {USB_REQ_TYP_CLASS | USB_REQ_RECIP_INTERF, HID_SET_IDLE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
__code unsigned char  GetHIDReport[] = {USB_REQ_TYP_IN | USB_REQ_RECIP_INTERF, USB_GET_DESCRIPTOR, 0x00, USB_DESCR_TYP_REPORT, 0 /*interface*/, 0x00, 0xff, 0x00};

// Hub class requests (USB 2.0 spec table 11-16). wValueL/wIndexL are
// overwritten per-call (feature selector, port number) before each transfer;
// the templates only fix bRequestType/bRequest/wLength.
__code unsigned char HubGetHubDescriptorRequest[] = {HUB_GET_HUB_DESCRIPTOR, USB_GET_DESCRIPTOR, 0x00, USB_DESCR_TYP_HUB, 0x00, 0x00, sizeof(USB_HUB_DESCR), 0x00};
__code unsigned char HubGetPortStatusRequest[] = {HUB_GET_PORT_STATUS, HUB_GET_STATUS, 0x00, 0x00, 0 /*port*/, 0x00, 0x04, 0x00};
__code unsigned char HubSetPortFeatureRequest[] = {HUB_SET_PORT_FEATURE, HUB_SET_FEATURE, 0 /*feature*/, 0x00, 0 /*port*/, 0x00, 0x00, 0x00};
__code unsigned char HubClearPortFeatureRequest[] = {HUB_CLEAR_PORT_FEATURE, HUB_CLEAR_FEATURE, 0 /*feature*/, 0x00, 0 /*port*/, 0x00, 0x00, 0x00};

__at(0x0000) unsigned char __xdata RxBuffer[MAX_PACKET_SIZE];
__at(0x0100) unsigned char __xdata TxBuffer[MAX_PACKET_SIZE];

__xdata uint8_t endpoint0Size;	//todo rly global?
unsigned char SetPort = 0;	//todo really global?

#define RECEIVE_BUFFER_LEN    512
__xdata unsigned char receiveDataBuffer[RECEIVE_BUFFER_LEN];

struct _RootHubDevice
{
	unsigned char status;
	unsigned char address;
	unsigned char speed;
} __xdata rootHubDevice[ROOT_HUB_COUNT];

void disableRootHubPort(unsigned char index)
{
	rootHubDevice[index].status = ROOT_DEVICE_DISCONNECT;
	rootHubDevice[index].address = 0;
	if (index)
	UHUB1_CTRL = 0;
	else
	UHUB0_CTRL = 0;
}

void initUSB_Host()
{
	IE_USB = 0;
	USB_CTRL = bUC_HOST_MODE;
	USB_DEV_AD = 0x00;
	UH_EP_MOD = bUH_EP_TX_EN | bUH_EP_RX_EN ;
	UH_RX_DMA = 0x0000;
	UH_TX_DMA = 0x0001;
	UH_RX_CTRL = 0x00;
	UH_TX_CTRL = 0x00;
	USB_CTRL = bUC_HOST_MODE | bUC_INT_BUSY | bUC_DMA_EN;
	UH_SETUP = bUH_SOF_EN;
	USB_INT_FG = 0xFF;

	disableRootHubPort(0);
	disableRootHubPort(1);
	USB_INT_EN = bUIE_TRANSFER | bUIE_DETECT;
}

void setHostUsbAddr(unsigned char addr)
{
	USB_DEV_AD = USB_DEV_AD & bUDA_GP_BIT | addr & 0x7F;
}

void setUsbSpeed(unsigned char fullSpeed)
{
	if (fullSpeed)
	{
		USB_CTRL &= ~ bUC_LOW_SPEED;
		UH_SETUP &= ~ bUH_PRE_PID_EN;
	}
	else
		USB_CTRL |= bUC_LOW_SPEED;
}

void resetRootHubPort(unsigned char rootHubIndex)
{
	endpoint0Size = DEFAULT_ENDP0_SIZE; //todo what's that?                    
	setHostUsbAddr(0);
	setUsbSpeed(1);
	 if (rootHubIndex == 0)    
    {
        UHUB0_CTRL = UHUB0_CTRL & ~ bUH_LOW_SPEED | bUH_BUS_RESET;
        delay(15);
        UHUB0_CTRL = UHUB0_CTRL & ~ bUH_BUS_RESET;
    }
    else if (rootHubIndex == 1)
    {
        UHUB1_CTRL = UHUB1_CTRL & ~ bUH_LOW_SPEED | bUH_BUS_RESET;
        delay(15);
        UHUB1_CTRL = UHUB1_CTRL & ~ bUH_BUS_RESET;
    }
	delayUs(250);
	UIF_DETECT = 0; //todo test if redundant                                       
}

unsigned char enableRootHubPort(unsigned char rootHubIndex)
{
    if ( rootHubDevice[ rootHubIndex ].status < 1 )
    {
        rootHubDevice[ rootHubIndex ].status = 1;
    }
	if (rootHubIndex == 0)
	{
		if (USB_HUB_ST & bUHS_H0_ATTACH)
		{
			if ((UHUB0_CTRL & bUH_PORT_EN) == 0x00)
			{
				if (USB_HUB_ST & bUHS_DM_LEVEL)
				{
					rootHubDevice[rootHubIndex].speed = 0;
					UHUB0_CTRL |= bUH_LOW_SPEED;
				}
				else rootHubDevice[rootHubIndex].speed = 1;
			}
			UHUB0_CTRL |= bUH_PORT_EN;
			return ERR_SUCCESS;
		}
	}
	else if (rootHubIndex == 1)
	{
		if (USB_HUB_ST & bUHS_H1_ATTACH)
		{
			if ((UHUB1_CTRL & bUH_PORT_EN ) == 0x00)
			{
				if (USB_HUB_ST & bUHS_HM_LEVEL)
				{
					rootHubDevice[rootHubIndex].speed = 0;
					UHUB1_CTRL |= bUH_LOW_SPEED;
				}
				else rootHubDevice[rootHubIndex].speed = 1;
			}
			UHUB1_CTRL |= bUH_PORT_EN;
			return ERR_SUCCESS;
		}
	}
	return ERR_USB_DISCON;
}

// selectHubPort() is defined further down, after the HubDevice[] struct it
// references is fully declared (its member layout must be visible to SDCC
// at the point of use, a plain forward tag declaration is not enough for
// member access). Prototype here so callers above that point still compile.
void selectHubPort(unsigned char rootHubIndex, unsigned char hubPort);

unsigned char hostTransfer(unsigned char endp_pid, unsigned char tog, unsigned short timeout )
{
    unsigned short retries;
    unsigned char	r;
    unsigned short	i;
    UH_RX_CTRL = tog;
    UH_TX_CTRL = tog;
    retries = 0;
    do
    {
        UH_EP_PID = endp_pid;                               
        UIF_TRANSFER = 0;            
        for (i = 200; i != 0 && UIF_TRANSFER == 0; i--)
            delayUs(1);
        UH_EP_PID = 0x00;                                         
        if ( UIF_TRANSFER == 0 )
        {
            return ERR_USB_UNKNOWN;
        }
        if ( UIF_TRANSFER )                                    
        {
            if ( U_TOG_OK )
            {
                return( ERR_SUCCESS );
            }
            r = USB_INT_ST & MASK_UIS_H_RES;               
            if ( r == USB_PID_STALL )
            {
                return( r | ERR_USB_TRANSFER );
            }
            if ( r == USB_PID_NAK )
            {
                if ( timeout == 0 )
                {
                    return( r | ERR_USB_TRANSFER );
                }
                if ( timeout < 0xFFFF )
                {
                    timeout --;
                }
                retries--;
            }
            else switch ( endp_pid >> 4 )	//todo no return.. compare to other guy
                {
                case USB_PID_SETUP:
                case USB_PID_OUT:
                    if ( U_TOG_OK )
                    {
                        return( ERR_SUCCESS );
                    }
                    if ( r == USB_PID_ACK )
                    {
                        return( ERR_SUCCESS );
                    }
                    if ( r == USB_PID_STALL || r == USB_PID_NAK )
                    {
                        return( r | ERR_USB_TRANSFER );
                    }
                    if ( r )
                    {
                        return( r | ERR_USB_TRANSFER );          
                    }
                    break;                                    
                case USB_PID_IN:
                    if ( U_TOG_OK )
                    {
                        return( ERR_SUCCESS );
                    }
                    if ( tog ? r == USB_PID_DATA1 : r == USB_PID_DATA0 )
                    {
                        return( ERR_SUCCESS );
                    }
                    if ( r == USB_PID_STALL || r == USB_PID_NAK )
                    {
                        return( r | ERR_USB_TRANSFER );
                    }
                    if ( r == USB_PID_DATA0 && r == USB_PID_DATA1 )
                    {
                    }                                        
                    else if ( r )
                    {
                        return( r | ERR_USB_TRANSFER );     
                    }
                    break;                                     
                default:
                    return( ERR_USB_UNKNOWN );                  
                    break;
                }
        }
        else                                                    
        {
            USB_INT_FG = 0xFF;                               
        }
        delayUs(15);
    }
    while ( ++retries < 200 );
    return( ERR_USB_TRANSFER );                              
}

//todo request buffer
unsigned char hostCtrlTransfer(unsigned char __xdata *DataBuf, unsigned short *RetLen, unsigned short maxLenght)
{
	unsigned char temp = maxLenght;
	unsigned short RemLen;
	unsigned char s, RxLen, i;
	unsigned char __xdata *pBuf;
	unsigned short *pLen;
	DEBUG_OUT("hostCtrlTransfer\n");
	PXUSB_SETUP_REQ pSetupReq = ((PXUSB_SETUP_REQ)TxBuffer);
	pBuf = DataBuf;
	pLen = RetLen;
	delayUs(200);
	if (pLen)
		*pLen = 0;
	UH_TX_LEN = sizeof(USB_SETUP_REQ);
	s = hostTransfer((unsigned char)(USB_PID_SETUP << 4), 0, 10000);
	if (s != ERR_SUCCESS)
		return (s);
	UH_RX_CTRL = UH_TX_CTRL = bUH_R_TOG | bUH_R_AUTO_TOG | bUH_T_TOG | bUH_T_AUTO_TOG;
	UH_TX_LEN = 0x01;
	RemLen = (pSetupReq->wLengthH << 8) | (pSetupReq->wLengthL);
	if (RemLen && pBuf)
	{ 
		if (pSetupReq->bRequestType & USB_REQ_TYP_IN)
		{
			DEBUG_OUT("Remaining bytes to read %d\n", RemLen);
			while (RemLen)
			{
				delayUs(300);
				s = hostTransfer((unsigned char)(USB_PID_IN << 4), UH_RX_CTRL, 10000); 
				if (s != ERR_SUCCESS)
					return (s);
				RxLen = USB_RX_LEN < RemLen ? USB_RX_LEN : RemLen;
				RemLen -= RxLen;
				if (pLen)
					*pLen += RxLen;
				for(i = 0; i < RxLen; i++)
					pBuf[i] = RxBuffer[i];
				pBuf += RxLen;
				DEBUG_OUT("Received %i bytes\n", (uint16_t)USB_RX_LEN);
				if (USB_RX_LEN == 0 || (USB_RX_LEN < endpoint0Size ))
					break; 
			}
			UH_TX_LEN = 0x00;
		}
		else
		{
			DEBUG_OUT("Remaining bytes to write %i", RemLen);
			//todo rework this TxBuffer overwritten
			while (RemLen)
			{
				delayUs(200);
				UH_TX_LEN = RemLen >= endpoint0Size ? endpoint0Size : RemLen;
				//memcpy(TxBuffer, pBuf, UH_TX_LEN);
				pBuf += UH_TX_LEN;
				if (pBuf[1] == 0x09)
				{
					SetPort = SetPort ^ 1 ? 1 : 0;
					*pBuf = SetPort;

					DEBUG_OUT("SET_PORT  %02X  %02X ", *pBuf, SetPort);
				}
				DEBUG_OUT("Sending %i bytes\n", (uint16_t)UH_TX_LEN);
				s = hostTransfer(USB_PID_OUT << 4, UH_TX_CTRL, 10000);
				if (s != ERR_SUCCESS)
					return (s);
				RemLen -= UH_TX_LEN;
				if (pLen)
					*pLen += UH_TX_LEN;
			}
		}
	}
	delayUs(200);
	s = hostTransfer((UH_TX_LEN ? USB_PID_IN << 4 : USB_PID_OUT << 4), bUH_R_TOG | bUH_T_TOG, 10000);
	if (s != ERR_SUCCESS)
		return (s);
	if (UH_TX_LEN == 0)
		return (ERR_SUCCESS);
	if (USB_RX_LEN == 0)
		return (ERR_SUCCESS);
	return (ERR_USB_BUF_OVER);
}

void fillTxBuffer(PUINT8C data, unsigned char len)
{
	unsigned char i;
	DEBUG_OUT("fillTxBuffer %i bytes\n", len);
	for(i = 0; i < len; i++)
		TxBuffer[i] = data[i];
	DEBUG_OUT("fillTxBuffer done\n", len);
}

unsigned char getDeviceDescriptor()
{
    unsigned char s;
    unsigned short len;
    endpoint0Size = DEFAULT_ENDP0_SIZE;		//TODO again?
	DEBUG_OUT("getDeviceDescriptor\n");
	fillTxBuffer(GetDeviceDescriptorRequest, sizeof(GetDeviceDescriptorRequest));
    s = hostCtrlTransfer(receiveDataBuffer, &len, RECEIVE_BUFFER_LEN);          
    if (s != ERR_SUCCESS)
        return s;

	DEBUG_OUT("Device descriptor request sent successfully\n");
    endpoint0Size = ((PXUSB_DEV_DESCR)receiveDataBuffer)->bMaxPacketSize0;
    if (len < ((PUSB_SETUP_REQ)GetDeviceDescriptorRequest)->wLengthL)
    {
		DEBUG_OUT("Received packet is smaller than expected\n")
        return ERR_USB_BUF_OVER;                
    }
    return ERR_SUCCESS;
}

unsigned char setUsbAddress(unsigned char addr)
{
    unsigned char s;
	PXUSB_SETUP_REQ pSetupReq = ((PXUSB_SETUP_REQ)TxBuffer);
    fillTxBuffer(SetUSBAddressRequest, sizeof(SetUSBAddressRequest));
    pSetupReq->wValueL = addr;          
    s = hostCtrlTransfer(0, 0, 0);   
    if (s != ERR_SUCCESS) return s;
    DEBUG_OUT( "SetAddress: %i\n" , addr);
    setHostUsbAddr(addr);
    delay(100);         
    return ERR_SUCCESS;
}

unsigned char setUsbConfig( unsigned char cfg )
{
	PXUSB_SETUP_REQ pSetupReq = ((PXUSB_SETUP_REQ)TxBuffer);
    fillTxBuffer(SetupSetUsbConfig, sizeof(SetupSetUsbConfig));
    pSetupReq->wValueL = cfg;                          
    return( hostCtrlTransfer(0, 0, 0) );            
}

unsigned char getDeviceString()
{
    fillTxBuffer(GetDeviceStringRequest, sizeof(GetDeviceStringRequest));                         
    return hostCtrlTransfer(receiveDataBuffer, 0, RECEIVE_BUFFER_LEN);
}

char convertStringDescriptor(unsigned char __xdata *usbBuffer, unsigned char __xdata *strBuffer, unsigned short bufferLength, unsigned char index)
{
	//supports using source as target buffer
	unsigned char i = 0, len = (usbBuffer[0] - 2) >> 1;
	if(usbBuffer[1] != 3) return 0;	//check if device string
	for(; (i < len) && (i < bufferLength - 1); i++)
		if(usbBuffer[2 + 1 + (i << 1)])
			strBuffer[i] = '?';
		else
			strBuffer[i] = usbBuffer[2 + (i << 1)];
	strBuffer[i] = 0;
	sendProtocolMSG(MSG_TYPE_DEVICE_STRING,(unsigned short)len, index+1, 0x34, 0x56, strBuffer);
	return 1;
}

void DEBUG_OUT_USB_BUFFER(unsigned char __xdata *usbBuffer)
{
	int i;
	for(i = 0; i < usbBuffer[0]; i++)
	{
		DEBUG_OUT("0x%02X ", (uint16_t)(usbBuffer[i]));
	}
	DEBUG_OUT("\n");
}

unsigned char getConfigurationDescriptor()
{
    unsigned char s;
    unsigned short len, total;
	fillTxBuffer(GetConfigurationDescriptorRequest, sizeof(GetConfigurationDescriptorRequest));

    s = hostCtrlTransfer(receiveDataBuffer, &len, RECEIVE_BUFFER_LEN);             
    if(s != ERR_SUCCESS)
        return s;
	//todo didnt send reqest completely
    if(len < ((PUSB_SETUP_REQ)GetConfigurationDescriptorRequest)->wLengthL)
        return ERR_USB_BUF_OVER;

	//todo fix 16bits
    total = ((PXUSB_CFG_DESCR)receiveDataBuffer)->wTotalLengthL + (((PXUSB_CFG_DESCR)receiveDataBuffer)->wTotalLengthH << 8);
	fillTxBuffer(GetConfigurationDescriptorRequest, sizeof(GetConfigurationDescriptorRequest));
    ((PUSB_SETUP_REQ)TxBuffer)->wLengthL = (unsigned char)(total & 255);
    ((PUSB_SETUP_REQ)TxBuffer)->wLengthH = (unsigned char)(total >> 8);
    s = hostCtrlTransfer(receiveDataBuffer, &len, RECEIVE_BUFFER_LEN);             
    if(s != ERR_SUCCESS)
        return s;
	//todo 16bit and fix received length check
	//if (len < total || len < ((PXUSB_CFG_DESCR)receiveDataBuffer)->wTotalLengthL)
    //    return( ERR_USB_BUF_OVER );                             
    return ERR_SUCCESS;
}

unsigned char getInterfaceDescriptor(unsigned char index)
{
	unsigned char temp = index;
	unsigned char s;
    unsigned short len;
	fillTxBuffer(GetInterfaceDescriptorRequest, sizeof(GetInterfaceDescriptorRequest));
    s = hostCtrlTransfer(receiveDataBuffer, &len, RECEIVE_BUFFER_LEN);             
	return s;                          
}

#define REPORT_USAGE_PAGE 		0x04
#define REPORT_USAGE 			0x08
#define REPORT_LOCAL_MINIMUM 	0x14
#define REPORT_LOCAL_MAXIMUM 	0x24
#define REPORT_PHYSICAL_MINIMUM 0x34
#define REPORT_PHYSICAL_MAXIMUM 0x44
#define REPORT_USAGE_MINIMUM	0x18
#define REPORT_USAGE_MAXIMUM	0x28

#define REPORT_UNIT				0x64
#define REPORT_INPUT			0x80
#define REPORT_OUTPUT 			0x90
#define REPORT_FEATURE			0xB0

#define REPORT_REPORT_SIZE		0x74
#define REPORT_REPORT_ID		0x84
#define REPORT_REPORT_COUNT		0x94

#define REPORT_COLLECTION		0xA0
#define REPORT_COLLECTION_END	0xC0

#define REPORT_USAGE_UNKNOWN	0x00
#define REPORT_USAGE_POINTER	0x01
#define REPORT_USAGE_MOUSE		0x02
#define REPORT_USAGE_RESERVED	0x03
#define REPORT_USAGE_JOYSTICK	0x04
#define REPORT_USAGE_GAMEPAD	0x05
#define REPORT_USAGE_KEYBOARD	0x06
#define REPORT_USAGE_KEYPAD		0x07
#define REPORT_USAGE_MULTI_AXIS	0x08
#define REPORT_USAGE_SYSTEM		0x09

#define REPORT_USAGE_X			0x30
#define REPORT_USAGE_Y			0x31
#define REPORT_USAGE_Z			0x32
#define REPORT_USAGE_Rx			0x33
#define REPORT_USAGE_Ry			0x34
#define REPORT_USAGE_Rz			0x35
#define REPORT_USAGE_WHEEL		0x38

#define REPORT_USAGE_PAGE_GENERIC	0x01
#define REPORT_USAGE_PAGE_KEYBOARD 	0x07
#define REPORT_USAGE_PAGE_LEDS		0x08
#define REPORT_USAGE_PAGE_BUTTON	0x09
#define REPORT_USAGE_PAGE_VENDOR	0xff00

#define MAX_HID_DEVICES 8
struct
{
	unsigned char connected;
	unsigned char rootHub;
	unsigned char hubPort; // HUB_PORT_DIRECT or 1..N: which physical device this is
	unsigned char interface;
	unsigned char endPoint;
	unsigned long type;
	// VID/PID captured for THIS device at enumeration time. Previously looked
	// up from a per-root-hub VendorProductID[] table, which broke once a hub
	// could put more than one HID device behind a single root port.
	unsigned char idVendorL, idVendorH, idProductL, idProductH;
}  __xdata HIDdevice[MAX_HID_DEVICES];

// CDC-ACM (USB virtual COM) device support. Each root port can host either
// one direct CDC device or (if a hub is attached) one CDC device per hub
// downstream port, so this is sized like HIDdevice rather than per-root-hub.
#define MAX_CDC_DEVICES 8
#define USB_DEV_CLASS_CDC_DATA 0x0A
#define CDC_SET_LINE_CODING        0x20
#define CDC_SET_CONTROL_LINE_STATE 0x22
struct
{
	unsigned char connected;
	unsigned char rootHub;
	unsigned char hubPort;       // HUB_PORT_DIRECT or 1..N
	unsigned char commInterface; // control interface (class 0x02), target of CDC requests
	unsigned char endPoint;      // bulk IN endpoint number, bit 7 = next data toggle
}  __xdata CDCdevice[MAX_CDC_DEVICES];

// One external hub per root port, single level (a hub plugged into this
// hub's own downstream ports is not enumerated — MAX_EXHUB_LEVEL stays 1).
// Addresses for downstream devices are taken from a range disjoint from the
// direct-device addresses used by initializeRootHubConnection() (2 and 3),
// see HUB_PORT_ADDR_BASE below.
struct _HubDevice
{
	unsigned char connected;
	unsigned char address;
	unsigned char nbrPorts;                          // clamped to MAX_EXHUB_PORT_COUNT
	unsigned char portConnected[MAX_EXHUB_PORT_COUNT];
	unsigned char portAddress[MAX_EXHUB_PORT_COUNT];
	unsigned char portSpeed[MAX_EXHUB_PORT_COUNT];    // 1 = full speed, 0 = low speed
}  __xdata HubDevice[ROOT_HUB_COUNT];

// Downstream device addresses start at 16 to stay clear of the direct-device
// address range (2/3, also reused for the hub's own address — a hub takes
// the same address slot a directly-connected device would have used).
#define HUB_PORT_ADDR_BASE 16

// Point the host controller's address/speed at either the device directly on
// a root port (hubPort == HUB_PORT_DIRECT) or at a specific downstream port
// of the external hub attached to that root port (hubPort == 1..N). Must be
// called before every control or data transfer targeting that device — the
// CH559 host controller has a single active address/speed pair, there is no
// per-transaction addressing.
void selectHubPort(unsigned char rootHubIndex, unsigned char hubPort)
{
	if (hubPort == HUB_PORT_DIRECT)
	{
		setHostUsbAddr(rootHubDevice[rootHubIndex].address);
		setUsbSpeed(rootHubDevice[rootHubIndex].speed);
	}
	else
	{
		setHostUsbAddr(HubDevice[rootHubIndex].portAddress[hubPort - 1]);
		setUsbSpeed(HubDevice[rootHubIndex].portSpeed[hubPort - 1]);
	}
}

__code unsigned char SetCDCLineCodingRequest[] = {USB_REQ_TYP_CLASS | USB_REQ_RECIP_INTERF, CDC_SET_LINE_CODING, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00};
__code unsigned char SetCDCControlLineStateRequest[] = {USB_REQ_TYP_CLASS | USB_REQ_RECIP_INTERF, CDC_SET_CONTROL_LINE_STATE, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}; // DTR | RTS
__code unsigned char CDCLineCoding115200[] = {0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08}; // 115200 baud, 1 stop, no parity, 8 data bits

// resetHubDevices() clears everything associated with one ROOT PORT: any
// direct device, any hub attached to it, and every device behind that hub.
// hubPort disambiguation means we can no longer clear by rootHub alone for
// CDCdevice[] (there can be several, one per hub downstream port), so it
// walks the array the same way HIDdevice[] does.
void resetHubDevices(unsigned char hubindex)
{
	// Matches the pre-hub-support code's unconditional-zero-on-rootHub-match
	// semantics (no `&& connected` guard) — a stale/half-registered slot from
	// an aborted enumeration gets cleared the same as a fully connected one.
	__xdata unsigned char i;
	for (i = 0; i < MAX_HID_DEVICES; i++)
	{
		if (HIDdevice[i].rootHub == hubindex)
		{
			HIDdevice[i].connected = 0;
			HIDdevice[i].rootHub = 0;
			HIDdevice[i].hubPort = 0;
			HIDdevice[i].interface = 0;
			HIDdevice[i].endPoint = 0;
			HIDdevice[i].type = 0;
			HIDdevice[i].idVendorL = HIDdevice[i].idVendorH = 0;
			HIDdevice[i].idProductL = HIDdevice[i].idProductH = 0;
		}
	}
	for (i = 0; i < MAX_CDC_DEVICES; i++)
	{
		if (CDCdevice[i].rootHub == hubindex)
		{
			CDCdevice[i].connected = 0;
			CDCdevice[i].rootHub = 0;
			CDCdevice[i].hubPort = 0;
			CDCdevice[i].commInterface = 0;
			CDCdevice[i].endPoint = 0;
		}
	}
	HubDevice[hubindex].connected = 0;
	HubDevice[hubindex].address = 0;
	HubDevice[hubindex].nbrPorts = 0;
	for (i = 0; i < MAX_EXHUB_PORT_COUNT; i++)
	{
		HubDevice[hubindex].portConnected[i] = 0;
		HubDevice[hubindex].portAddress[i] = 0;
		HubDevice[hubindex].portSpeed[i] = 0;
	}
}

void pollHIDdevice()
{
	__xdata unsigned char s, hiddevice, len;
	for (hiddevice = 0; hiddevice < MAX_HID_DEVICES; hiddevice++)
	{
		if(HIDdevice[hiddevice].connected){
		selectHubPort(HIDdevice[hiddevice].rootHub, HIDdevice[hiddevice].hubPort);
		s = hostTransfer( USB_PID_IN << 4 | HIDdevice[hiddevice].endPoint & 0x7F, HIDdevice[hiddevice].endPoint & 0x80 ? bUH_R_TOG | bUH_T_TOG : 0, 0 );
		if ( s == ERR_SUCCESS )
   		{
    		HIDdevice[hiddevice].endPoint ^= 0x80;
			len = USB_RX_LEN;
			if ( len )
			{
				LED = !LED;
				sendHidPollMSG(MSG_TYPE_DEVICE_POLL,len, HIDdevice[hiddevice].type, hiddevice, HIDdevice[hiddevice].endPoint & 0x7F, RxBuffer,HIDdevice[hiddevice].idVendorL,HIDdevice[hiddevice].idVendorH,HIDdevice[hiddevice].idProductL,HIDdevice[hiddevice].idProductH);
			}
		}
		}
	}
}


// SET_LINE_CODING needs an OUT data stage, which hostCtrlTransfer does not
// support (its write path never copies the payload into TxBuffer). Run the
// three stages manually: SETUP (DATA0), DATA OUT (DATA1), STATUS IN (DATA1).
unsigned char cdcSetLineCoding(unsigned char commInterface)
{
	unsigned char s, i;
	fillTxBuffer(SetCDCLineCodingRequest, sizeof(SetCDCLineCodingRequest));
	((PXUSB_SETUP_REQ)TxBuffer)->wIndexL = commInterface;
	UH_TX_LEN = sizeof(USB_SETUP_REQ);
	s = hostTransfer((unsigned char)(USB_PID_SETUP << 4), 0, 10000);
	if (s != ERR_SUCCESS)
		return s;
	for (i = 0; i < sizeof(CDCLineCoding115200); i++)
		TxBuffer[i] = CDCLineCoding115200[i];
	UH_TX_LEN = sizeof(CDCLineCoding115200);
	s = hostTransfer((unsigned char)(USB_PID_OUT << 4), bUH_R_TOG | bUH_T_TOG, 10000);
	if (s != ERR_SUCCESS)
		return s;
	UH_TX_LEN = 0;
	return hostTransfer((unsigned char)(USB_PID_IN << 4), bUH_R_TOG | bUH_T_TOG, 10000);
}

// deviceTag() gives the ESP32 side a single byte that identifies a physical
// USB device across both root ports and every hub downstream port.
// Regression guard: for a device connected DIRECTLY to a root port
// (hubPort == HUB_PORT_DIRECT), this returns exactly rootHubIndex + 1 — the
// same 1 or 2 the pre-hub-support firmware always sent for
// MSG_TYPE_CDC_DATA/CONNECTED/DISCONNECTED. That is the only topology in
// production today, so every existing frame's `device` byte is byte-for-byte
// unchanged. Only devices behind an external hub (hubPort 1..N) get the new
// tag range 16 + rootHubIndex*MAX_EXHUB_PORT_COUNT + hubPort, chosen to
// never collide with 1/2 so old and new topologies stay distinguishable on
// the same wire format.
unsigned char deviceTag(unsigned char rootHubIndex, unsigned char hubPort)
{
	if (hubPort == HUB_PORT_DIRECT)
		return rootHubIndex + 1;
	return HUB_PORT_ADDR_BASE + (rootHubIndex * MAX_EXHUB_PORT_COUNT) + hubPort;
}

unsigned char cdcInitDevice(unsigned char cdcIndex)
{
	unsigned char s;
	s = cdcSetLineCoding(CDCdevice[cdcIndex].commInterface);
	DEBUG_OUT("CDC SET_LINE_CODING: %02X\n", s);
	// Line coding is informational for pure-USB devices; don't fail on it.
	fillTxBuffer(SetCDCControlLineStateRequest, sizeof(SetCDCControlLineStateRequest));
	((PXUSB_SETUP_REQ)TxBuffer)->wIndexL = CDCdevice[cdcIndex].commInterface;
	s = hostCtrlTransfer(0, 0, 0);
	DEBUG_OUT("CDC SET_CONTROL_LINE_STATE: %02X\n", s);
	return s;
}

void pollCDCdevice()
{
	__xdata unsigned char s, i, len;
	for (i = 0; i < MAX_CDC_DEVICES; i++)
	{
		if (!CDCdevice[i].connected)
			continue;
		selectHubPort(CDCdevice[i].rootHub, CDCdevice[i].hubPort);
		s = hostTransfer( USB_PID_IN << 4 | CDCdevice[i].endPoint & 0x7F, CDCdevice[i].endPoint & 0x80 ? bUH_R_TOG | bUH_T_TOG : 0, 0 );
		if (s == ERR_SUCCESS)
		{
			CDCdevice[i].endPoint ^= 0x80;
			len = USB_RX_LEN;
			if (len)
			{
				LED = !LED;
				sendProtocolMSG(MSG_TYPE_CDC_DATA, len, 0x00, deviceTag(CDCdevice[i].rootHub, CDCdevice[i].hubPort), CDCdevice[i].endPoint & 0x7F, RxBuffer);
			}
		}
	}
}

void parseHIDDeviceReport(unsigned char __xdata *report, unsigned short length, unsigned char CurrentDevive)
{
	unsigned short i = 0;
	unsigned char level = 0;
	unsigned char isUsageSet = 0;
	while(i < length)
	{
		unsigned char j;
		unsigned char id = report[i] & 0b11111100;
		unsigned char size = report[i] & 0b00000011;
		unsigned long data = 0;
		if(size == 3) size++;
		for(j = 0; j < size; j++)
			data |= ((unsigned long)report[i + 1 + j]) << (j * 8);
		for(j = 0; j < level - (id == REPORT_COLLECTION_END ? 1 : 0); j++)
			DEBUG_OUT("    ");
		switch(id)
		{
			case REPORT_USAGE_PAGE:	//todo clean up defines (case)
			{
				unsigned long vd = data < REPORT_USAGE_PAGE_VENDOR ? data : REPORT_USAGE_PAGE_VENDOR;
				DEBUG_OUT("Usage page ");
				switch(vd)
				{
					case REPORT_USAGE_PAGE_LEDS:
						DEBUG_OUT("LEDs");
					break;
					case REPORT_USAGE_PAGE_KEYBOARD:
						DEBUG_OUT("Keyboard/Keypad");
					break;
					case REPORT_USAGE_PAGE_BUTTON:
						DEBUG_OUT("Button");
					break;
					case REPORT_USAGE_PAGE_GENERIC:
						DEBUG_OUT("generic desktop controls");
					break;
					case REPORT_USAGE_PAGE_VENDOR:
						DEBUG_OUT("vendor defined 0x%04lx", data);
					break;
					default:
						DEBUG_OUT("unknown 0x%02lx", data);
				}
				DEBUG_OUT("\n");
			}
			break;
			case REPORT_USAGE:
				if (!isUsageSet){
					HIDdevice[CurrentDevive].type = data;
					isUsageSet = 1;
				}
				DEBUG_OUT("Usage ");
				switch(data)
				{
					case REPORT_USAGE_UNKNOWN:
						DEBUG_OUT("Unknown");
					break;
					case REPORT_USAGE_POINTER:
						DEBUG_OUT("Pointer");
					break;
					case REPORT_USAGE_MOUSE:
						DEBUG_OUT("Mouse");
					break;
					case REPORT_USAGE_RESERVED:
						DEBUG_OUT("Reserved");
					break;
					case REPORT_USAGE_JOYSTICK:
						DEBUG_OUT("Joystick");
					break;
					case REPORT_USAGE_GAMEPAD:
						DEBUG_OUT("Gamepad");
					break;
					case REPORT_USAGE_KEYBOARD:
						DEBUG_OUT("Keyboard");
					break;
					case REPORT_USAGE_KEYPAD:
						DEBUG_OUT("Keypad");
					break;
					case REPORT_USAGE_MULTI_AXIS:
						DEBUG_OUT("Multi-Axis controller");
					break;
					case REPORT_USAGE_SYSTEM:
						DEBUG_OUT("Tablet system controls");
					break;

					case REPORT_USAGE_X:
						DEBUG_OUT("X");
					break;
					case REPORT_USAGE_Y:
						DEBUG_OUT("Y");
					break;
					case REPORT_USAGE_Z:
						DEBUG_OUT("Z");
					break;
					case REPORT_USAGE_WHEEL:
						DEBUG_OUT("Wheel");
					break;
					default:
						DEBUG_OUT("unknown 0x%02lx", data);
				}
				DEBUG_OUT("\n");
			break;
			case REPORT_LOCAL_MINIMUM:
				DEBUG_OUT("Logical min %lu\n", data);
			break;
			case REPORT_LOCAL_MAXIMUM:
				DEBUG_OUT("Logical max %lu\n", data);
			break;
			case REPORT_PHYSICAL_MINIMUM:
				DEBUG_OUT("Physical min %lu\n", data);
			break;
			case REPORT_PHYSICAL_MAXIMUM:
				DEBUG_OUT("Physical max %lu\n", data);
			break;
			case REPORT_USAGE_MINIMUM:
				DEBUG_OUT("Physical min %lu\n", data);
			break;
			case REPORT_USAGE_MAXIMUM:
				DEBUG_OUT("Physical max %lu\n", data);
			break;
			case REPORT_COLLECTION:
				DEBUG_OUT("Collection start %lu\n", data);
				level++;
			break;
			case REPORT_COLLECTION_END:
				DEBUG_OUT("Collection end %lu\n", data);
				level--;
			break;
			case REPORT_UNIT:
				DEBUG_OUT("Unit 0x%02lx\n", data);
			break;
			case REPORT_INPUT:
				DEBUG_OUT("Input 0x%02lx\n", data);
			break;
			case REPORT_OUTPUT:
				DEBUG_OUT("Output 0x%02lx\n", data);
			break;
			case REPORT_FEATURE:
				DEBUG_OUT("Feature 0x%02lx\n", data);
			break;
			case REPORT_REPORT_SIZE:
				DEBUG_OUT("Report size %lu\n", data);
			break;
			case REPORT_REPORT_ID:
				DEBUG_OUT("Report ID %lu\n", data);
			break;
			case REPORT_REPORT_COUNT:
				DEBUG_OUT("Report count %lu\n", data);
			break;
			default:
				DEBUG_OUT("Unknown HID report identifier: 0x%02x (%i bytes) data: 0x%02lx\n", id, size, data);
		};
		i += size + 1;
	}
}

unsigned char getHIDDeviceReport(unsigned char CurrentDevive)
{
 	unsigned char s;
	unsigned short len, i, reportLen = RECEIVE_BUFFER_LEN;
	DEBUG_OUT("Requesting report from interface %i\n", HIDdevice[CurrentDevive].interface);

	fillTxBuffer(SetHIDIdleRequest, sizeof(SetHIDIdleRequest));
	((PXUSB_SETUP_REQ)TxBuffer)->wIndexL = HIDdevice[CurrentDevive].interface;	
	s = hostCtrlTransfer(receiveDataBuffer, &len, 0);
	
	//todo really dont care if successful? 8bitdo faild here
	//if(s != ERR_SUCCESS)
	//	return s;

	fillTxBuffer(GetHIDReport, sizeof(GetHIDReport));
	((PXUSB_SETUP_REQ)TxBuffer)->wIndexL = HIDdevice[CurrentDevive].interface;
	((PXUSB_SETUP_REQ)TxBuffer)->wLengthL = (unsigned char)(reportLen & 255); 
	((PXUSB_SETUP_REQ)TxBuffer)->wLengthH = (unsigned char)(reportLen >> 8);
	s = hostCtrlTransfer(receiveDataBuffer, &len, RECEIVE_BUFFER_LEN);
	if(s != ERR_SUCCESS)
		return s;
	
	for (i = 0; i < len; i++)
	{
		DEBUG_OUT("0x%02X ", receiveDataBuffer[i]);
	}
	DEBUG_OUT("\n");
	sendProtocolMSG(MSG_TYPE_HID_INFO, len, CurrentDevive, HIDdevice[CurrentDevive].interface, HIDdevice[CurrentDevive].rootHub, receiveDataBuffer);
	parseHIDDeviceReport(receiveDataBuffer, len, CurrentDevive);
	return (ERR_SUCCESS);
}

void readInterface(unsigned char rootHubIndex, PXUSB_ITF_DESCR interface)
{
	unsigned char temp = rootHubIndex;
	DEBUG_OUT("Interface %d\n", interface->bInterfaceNumber);
	DEBUG_OUT("  Class %d\n", interface->bInterfaceClass);
	DEBUG_OUT("  Sub Class %d\n", interface->bInterfaceSubClass);
	DEBUG_OUT("  Interface Protocol %d\n", interface->bInterfaceProtocol);
}

void readHIDInterface(PXUSB_ITF_DESCR interface, PXUSB_HID_DESCR descriptor)
{
	DEBUG_OUT("HID at Interface %d\n", interface->bInterfaceNumber);
	DEBUG_OUT("  USB %d.%d%d\n", (descriptor->bcdHIDH & 15), (descriptor->bcdHIDL >> 4), (descriptor->bcdHIDL & 15));
	DEBUG_OUT("  Country code 0x%02X\n", descriptor->bCountryCode);
	DEBUG_OUT("  TypeX 0x%02X\n", descriptor->bDescriptorTypeX);
}

void readEndpoint()
{
}

// Walks the (already-fetched-elsewhere) device/config descriptors of the
// device now sitting at `addr` and registers any HID/CDC interfaces it
// exposes into HIDdevice[]/CDCdevice[], tagged with (rootHubIndex, hubPort)
// so pollHIDdevice()/pollCDCdevice() know how to reach them later via
// selectHubPort(). This is the interface-walking body that used to live
// inline in initializeRootHubConnection() — factored out so it can also run
// once per downstream port of an external hub (see initializeHubDownstream-
// Device()) without duplicating ~150 lines per call site.
// Caller must have already done: resetRootHubPort/enableRootHubPort (or hub
// port reset/enable), selectHubPort(rootHubIndex, hubPort) at the OLD
// address (0 for a freshly reset device), getDeviceDescriptor(), and
// setUsbAddress(addr). This function starts from getDeviceString().
// idVendorL/H/idProductL/H must be captured by the caller from
// receiveDataBuffer right after getDeviceDescriptor() succeeded — by the
// time this function walks interfaces, receiveDataBuffer has long since
// been overwritten by getConfigurationDescriptor().
unsigned char enumerateDeviceAtAddress(unsigned char rootHubIndex, unsigned char hubPort, unsigned char addr,
	unsigned char idVendorL, unsigned char idVendorH, unsigned char idProductL, unsigned char idProductH)
{
	unsigned char s, cfg;
	s = getDeviceString();
	DEBUG_OUT_USB_BUFFER(receiveDataBuffer);
	if(convertStringDescriptor(receiveDataBuffer, receiveDataBuffer, RECEIVE_BUFFER_LEN, rootHubIndex))
	{
		DEBUG_OUT("Device String: %s\n", receiveDataBuffer);
	}
	s = getConfigurationDescriptor();
	if ( s == ERR_SUCCESS )
	{
		unsigned short i, total;
		unsigned char __xdata temp[512];
		PXUSB_ITF_DESCR currentInterface = 0;
		int interfaces;

		sendProtocolMSG(MSG_TYPE_DEVICE_INFO, (receiveDataBuffer[2] + (receiveDataBuffer[3] << 8)), addr, deviceTag(rootHubIndex, hubPort), 0xAA, receiveDataBuffer);

		cfg = ((PXUSB_CFG_DESCR)receiveDataBuffer)->bConfigurationValue;
		DEBUG_OUT("Configuration value: %d\n", cfg);

		interfaces = ((PXUSB_CFG_DESCR_LONG)receiveDataBuffer)->cfg_descr.bNumInterfaces;
		DEBUG_OUT("Interface count: %d\n", interfaces);

		s = setUsbConfig( cfg );
		//parse descriptors
		total = ((PXUSB_CFG_DESCR)receiveDataBuffer)->wTotalLengthL + (((PXUSB_CFG_DESCR)receiveDataBuffer)->wTotalLengthH << 8);
		for(i = 0; i < total; i++)
			temp[i] = receiveDataBuffer[i];
		i = ((PXUSB_CFG_DESCR)receiveDataBuffer)->bLength;
		while(i < total)
		{
			unsigned char __xdata *desc = &(temp[i]);
			switch(desc[1])
			{
				case USB_DESCR_TYP_INTERF:
					DEBUG_OUT("Interface descriptor found\n", desc[1]);
					currentInterface = ((PXUSB_ITF_DESCR)desc);
					readInterface(rootHubIndex, currentInterface);
					if(currentInterface->bInterfaceClass == USB_DEV_CLASS_COMMUNIC)
					{
						// CDC control interface: remember its number, CDC
						// class requests (line coding, DTR) target it. The
						// interface number is stashed on a not-yet-connected
						// slot for THIS (rootHub, hubPort) pair; connected
						// flips to 1 once the matching data interface's bulk
						// IN endpoint is found below. A free slot is any
						// slot with connected == 0 — rootHub/hubPort default
						// to 0 there too, but 0 is root port 0's legitimate
						// index, so connected (not rootHub) is what marks a
						// slot as actually in use.
						unsigned char cdcSlot;
						for (cdcSlot = 0; cdcSlot < MAX_CDC_DEVICES; cdcSlot++)
							if (!CDCdevice[cdcSlot].connected) break;
						if (cdcSlot < MAX_CDC_DEVICES)
						{
							CDCdevice[cdcSlot].commInterface = currentInterface->bInterfaceNumber;
							CDCdevice[cdcSlot].rootHub = rootHubIndex;
							CDCdevice[cdcSlot].hubPort = hubPort;
						}
					}
					break;
				case USB_DESCR_TYP_ENDP:
					DEBUG_OUT("Endpoint descriptor found\n", desc[1]);
					DEBUG_OUT_USB_BUFFER(desc);
					if(currentInterface->bInterfaceClass == USB_DEV_CLASS_HID)
					{
						PXUSB_ENDP_DESCR d = (PXUSB_ENDP_DESCR)desc;
						if(d->bEndpointAddress & 0x80){
							unsigned char hiddevice;
							for (hiddevice = 0; hiddevice < MAX_HID_DEVICES; hiddevice++)
							{
								if(HIDdevice[hiddevice].connected == 0)break;
							}
							DEBUG_OUT("Connected device at position: %i\n", hiddevice);
							// Bit 7 of endPoint stores the next expected data toggle.
							// After SET_CONFIGURATION the device endpoint starts at
							// DATA0, but bEndpointAddress has bit 7 set for IN
							// endpoints (e.g. 0x82) which made the first poll expect
							// DATA1. The toggle mismatch makes the host ACK-and-drop
							// the very first report (the device dequeues it), losing
							// the first character of the first scan after connect.
							// Clear bit 7 so polling starts expecting DATA0.
							HIDdevice[hiddevice].endPoint = d->bEndpointAddress & 0x7F;
							HIDdevice[hiddevice].connected = 1;
							HIDdevice[hiddevice].interface = currentInterface->bInterfaceNumber;
							HIDdevice[hiddevice].rootHub = rootHubIndex;
							HIDdevice[hiddevice].hubPort = hubPort;
							HIDdevice[hiddevice].idVendorL = idVendorL;
							HIDdevice[hiddevice].idVendorH = idVendorH;
							HIDdevice[hiddevice].idProductL = idProductL;
							HIDdevice[hiddevice].idProductH = idProductH;
							DEBUG_OUT("Got endpoint for the HIDdevice 0x%02x\n", HIDdevice[hiddevice].endPoint);
							getHIDDeviceReport(hiddevice);
						}
					}
					else if(currentInterface->bInterfaceClass == USB_DEV_CLASS_CDC_DATA)
					{
						PXUSB_ENDP_DESCR d = (PXUSB_ENDP_DESCR)desc;
						// Register the bulk IN endpoint of the CDC data interface.
						// Toggle (bit 7) starts at DATA0, same rationale as HID.
						if((d->bEndpointAddress & 0x80) && (d->bmAttributes & 0x03) == 0x02)
						{
							// The matching CDC control interface (class 0x02,
							// handled above) always precedes this data
							// interface in the descriptor list and reserved
							// a slot tagged with this exact (rootHub, hubPort)
							// — find that slot rather than the first free one,
							// so a second CDC device enumerated concurrently
							// on the other root port/hub port can't collide.
							unsigned char cdcSlot;
							for (cdcSlot = 0; cdcSlot < MAX_CDC_DEVICES; cdcSlot++)
								if (!CDCdevice[cdcSlot].connected && CDCdevice[cdcSlot].commInterface != 0
									&& CDCdevice[cdcSlot].rootHub == rootHubIndex && CDCdevice[cdcSlot].hubPort == hubPort) break;
							if (cdcSlot < MAX_CDC_DEVICES)
							{
								CDCdevice[cdcSlot].rootHub = rootHubIndex;
								CDCdevice[cdcSlot].hubPort = hubPort;
								CDCdevice[cdcSlot].endPoint = d->bEndpointAddress & 0x7F;
								CDCdevice[cdcSlot].connected = 1;
								DEBUG_OUT("Got bulk IN endpoint for the CDC device 0x%02x\n", d->bEndpointAddress);
							}
						}
					}
					break;
				case USB_DESCR_TYP_HID:
					DEBUG_OUT("HID descriptor found\n", desc[1]);
					if(currentInterface == 0) break;
					readHIDInterface(currentInterface, (PXUSB_HID_DESCR)desc);
					break;
				case USB_DESCR_TYP_CS_INTF:
					DEBUG_OUT("Class specific header descriptor found\n", desc[1]);
					DEBUG_OUT_USB_BUFFER(desc);
					break;
				case USB_DESCR_TYP_CS_ENDP:
					DEBUG_OUT("Class specific endpoint descriptor found\n", desc[1]);
					DEBUG_OUT_USB_BUFFER(desc);
					break;
				default:
					DEBUG_OUT("Unexpected descriptor type: %02X\n", desc[1]);
					DEBUG_OUT_USB_BUFFER(desc);
			}
			i += desc[0];
		}
		for (i = 0; i < MAX_CDC_DEVICES; i++)
			if (CDCdevice[i].rootHub == rootHubIndex && CDCdevice[i].hubPort == hubPort && CDCdevice[i].connected)
				cdcInitDevice(i);
		return ERR_SUCCESS;
	}
	return s;
}

// Resets, enables and enumerates one downstream port of the external hub
// already registered in HubDevice[rootHubIndex]. Mirrors the direct-device
// path in initializeRootHubConnection() but goes through the hub's own
// SET_PORT_FEATURE/GET_PORT_STATUS class requests instead of the CH559's
// root-port SFRs, since the CH559 hardware has no visibility into a hub's
// internal ports — only the hub's own control endpoint does.
unsigned char initializeHubDownstreamDevice(unsigned char rootHubIndex, unsigned char hubPortIndex)
{
	unsigned char s, addr, dv_cls;
	unsigned char idVendorL, idVendorH, idProductL, idProductH;
	unsigned short len;
	unsigned char __xdata portStatus[4];

	// Address the hub itself to issue the port reset.
	setHostUsbAddr(HubDevice[rootHubIndex].address);
	setUsbSpeed(1); // hubs are always full-speed-capable devices on this chip
	fillTxBuffer(HubSetPortFeatureRequest, sizeof(HubSetPortFeatureRequest));
	((PXUSB_SETUP_REQ)TxBuffer)->wValueL = HUB_PORT_RESET;
	((PXUSB_SETUP_REQ)TxBuffer)->wIndexL = hubPortIndex; // 1-based, per USB hub spec
	s = hostCtrlTransfer(0, 0, 0);
	if (s != ERR_SUCCESS)
	{
		DEBUG_OUT("Hub port %i reset failed: %02X\n", hubPortIndex, s);
		return s;
	}
	delay(50); // USB spec: >=10ms reset recovery before talking to the new device

	// Read back port status to learn the negotiated speed (low vs full) and
	// clear the C_PORT_RESET change bit the hub latched.
	fillTxBuffer(HubGetPortStatusRequest, sizeof(HubGetPortStatusRequest));
	((PXUSB_SETUP_REQ)TxBuffer)->wIndexL = hubPortIndex;
	s = hostCtrlTransfer(portStatus, &len, sizeof(portStatus));
	if (s != ERR_SUCCESS || len < 4)
	{
		DEBUG_OUT("Hub GET_PORT_STATUS failed: %02X\n", s);
		return ERR_USB_TRANSFER;
	}
	// wPortStatus bit 9 (byte1 bit1) = low speed. wPortChange (bytes 2-3) bit
	// 4 = C_PORT_RESET; clear it so the hub stops reporting the change.
	HubDevice[rootHubIndex].portSpeed[hubPortIndex - 1] = (portStatus[1] & 0x02) ? 0 : 1; // 0=low,1=full — matches rootHubDevice[].speed convention
	fillTxBuffer(HubClearPortFeatureRequest, sizeof(HubClearPortFeatureRequest));
	((PXUSB_SETUP_REQ)TxBuffer)->wValueL = HUB_C_PORT_RESET;
	((PXUSB_SETUP_REQ)TxBuffer)->wIndexL = hubPortIndex;
	hostCtrlTransfer(0, 0, 0); // best-effort; a failure here doesn't block enumeration

	// The freshly reset downstream device answers at address 0 until we
	// assign it one. Re-point the host controller at address 0, at the
	// speed we just learned.
	setHostUsbAddr(0);
	setUsbSpeed(HubDevice[rootHubIndex].portSpeed[hubPortIndex - 1]);
	s = getDeviceDescriptor();
	if (s != ERR_SUCCESS)
	{
		DEBUG_OUT("Hub port %i: get device descriptor failed %02X\n", hubPortIndex, s);
		return s;
	}
	dv_cls = ((PXUSB_DEV_DESCR)receiveDataBuffer)->bDeviceClass;
	DEBUG_OUT("Hub port %i device class %i\n", hubPortIndex, dv_cls);
	if (dv_cls == USB_DEV_CLASS_HUB)
	{
		// Not supported: only one hub level deep (MAX_EXHUB_LEVEL == 1).
		DEBUG_OUT("Hub-behind-hub at port %i not supported, ignoring\n", hubPortIndex);
		return ERR_USB_UNSUPPORT;
	}
	// Captured now: receiveDataBuffer is overwritten by the next control
	// transfer (getConfigurationDescriptor/getDeviceString inside
	// enumerateDeviceAtAddress).
	idVendorL = ((PXUSB_DEV_DESCR)receiveDataBuffer)->idVendorL;
	idVendorH = ((PXUSB_DEV_DESCR)receiveDataBuffer)->idVendorH;
	idProductL = ((PXUSB_DEV_DESCR)receiveDataBuffer)->idProductL;
	idProductH = ((PXUSB_DEV_DESCR)receiveDataBuffer)->idProductH;

	addr = HUB_PORT_ADDR_BASE + (rootHubIndex * MAX_EXHUB_PORT_COUNT) + (hubPortIndex - 1);
	s = setUsbAddress(addr);
	if (s != ERR_SUCCESS)
	{
		DEBUG_OUT("Hub port %i set address failed %02X\n", hubPortIndex, s);
		return s;
	}
	HubDevice[rootHubIndex].portAddress[hubPortIndex - 1] = addr;
	HubDevice[rootHubIndex].portConnected[hubPortIndex - 1] = 1;
	// setUsbAddress() already re-pointed the controller at `addr`; keep the
	// speed set for this port so the subsequent transfers use the right
	// signalling rate.
	setUsbSpeed(HubDevice[rootHubIndex].portSpeed[hubPortIndex - 1]);
	return enumerateDeviceAtAddress(rootHubIndex, hubPortIndex, addr, idVendorL, idVendorH, idProductL, idProductH);
}

// Fetches the hub descriptor, powers every downstream port (bus-powered
// hubs still need PORT_POWER issued before ports report connect status on
// most silicon), then enumerates whatever is already plugged into each
// port. Called once, right after we've discovered the directly-attached
// device at a root port is itself a hub (bDeviceClass == USB_DEV_CLASS_HUB).
unsigned char initializeHub(unsigned char rootHubIndex, unsigned char addr)
{
	unsigned char s, port, nbrPorts;
	unsigned short len;

	HubDevice[rootHubIndex].connected = 1;
	HubDevice[rootHubIndex].address = addr;

	fillTxBuffer(HubGetHubDescriptorRequest, sizeof(HubGetHubDescriptorRequest));
	s = hostCtrlTransfer(receiveDataBuffer, &len, RECEIVE_BUFFER_LEN);
	if (s != ERR_SUCCESS || len < sizeof(USB_HUB_DESCR))
	{
		DEBUG_OUT("Hub GET_HUB_DESCRIPTOR failed: %02X\n", s);
		return ERR_USB_TRANSFER;
	}
	nbrPorts = ((PXUSB_HUB_DESCR)receiveDataBuffer)->bNbrPorts;
	if (nbrPorts > MAX_EXHUB_PORT_COUNT) nbrPorts = MAX_EXHUB_PORT_COUNT;
	HubDevice[rootHubIndex].nbrPorts = nbrPorts;
	DEBUG_OUT("Hub at root %i has %i ports (clamped to %i)\n", rootHubIndex, ((PXUSB_HUB_DESCR)receiveDataBuffer)->bNbrPorts, nbrPorts);

	for (port = 1; port <= nbrPorts; port++)
	{
		fillTxBuffer(HubSetPortFeatureRequest, sizeof(HubSetPortFeatureRequest));
		((PXUSB_SETUP_REQ)TxBuffer)->wValueL = HUB_PORT_POWER;
		((PXUSB_SETUP_REQ)TxBuffer)->wIndexL = port;
		hostCtrlTransfer(0, 0, 0); // best-effort
	}
	delay(100); // bPwrOn2PwrGood is in the descriptor but 100ms covers common hubs

	s = ERR_SUCCESS;
	for (port = 1; port <= nbrPorts; port++)
	{
		unsigned char __xdata portStatus[4];
		setHostUsbAddr(addr);
		setUsbSpeed(1);
		fillTxBuffer(HubGetPortStatusRequest, sizeof(HubGetPortStatusRequest));
		((PXUSB_SETUP_REQ)TxBuffer)->wIndexL = port;
		if (hostCtrlTransfer(portStatus, &len, sizeof(portStatus)) != ERR_SUCCESS || len < 4)
			continue;
		if (!(portStatus[0] & 0x01)) // wPortStatus bit 0 = PORT_CONNECTION
			continue;
		DEBUG_OUT("Hub port %i has a device attached\n", port);
		initializeHubDownstreamDevice(rootHubIndex, port);
		// A single failed/unsupported downstream device (e.g. a nested hub)
		// must not abort enumeration of the hub's other ports.
	}
	return s;
}

// Enumerates whatever is attached directly to a CH559 root port.
//
// IMPORTANT — regression guard: everything up to and including the
// setUsbAddress()/rootHubDevice[].address assignment below is BYTE-FOR-BYTE
// identical to the pre-hub-support code path (same retry loop, same
// resetRootHubPort/enableRootHubPort sequence, same
// selectHubPort(rootHubIndex, 0) i.e. HUB_PORT_DIRECT, same
// GetDeviceDescriptorRequest -> SetUSBAddressRequest sequence, same
// addr = rootHubIndex + wValueL formula, same retry/error handling). A
// directly-connected scanner (the only topology in production today) takes
// exactly the same USB transactions in exactly the same order it always
// has; the only new behavior is the added dv_cls == USB_DEV_CLASS_HUB
// branch, which a scanner's device class (0x03 HID or 0x02/0x0A CDC) never
// takes. In particular the endpoint data-toggle fix (HIDdevice[].endPoint
// bit-7 clear before the first poll, see enumerateDeviceAtAddress()) is
// preserved unchanged — enumerateDeviceAtAddress() is the exact same code
// that used to run inline here, just moved into its own function.
unsigned char initializeRootHubConnection(unsigned char rootHubIndex)
{
	unsigned char retry, i, s = ERR_SUCCESS, dv_cls, addr;
	unsigned char idVendorL, idVendorH, idProductL, idProductH;

	for(retry = 0; retry < 10; retry++) //todo test fewer retries
	{
	delay( 100 );
		delay(100); //todo test lower delay
		resetHubDevices(rootHubIndex);
		resetRootHubPort(rootHubIndex);
		for (i = 0; i < 100; i++) //todo test fewer retries
		{
			delay(1);
			if (enableRootHubPort(rootHubIndex) == ERR_SUCCESS)
				break;
		}
		if (i == 100)
		{
			disableRootHubPort(rootHubIndex);
			DEBUG_OUT("Failed to enable root hub port %i\n", rootHubIndex);
			continue;
		}

		selectHubPort(rootHubIndex, HUB_PORT_DIRECT);
		DEBUG_OUT("root hub port %i enabled\n", rootHubIndex);
		s = getDeviceDescriptor();

		if ( s == ERR_SUCCESS )
		{
			dv_cls = ((PXUSB_DEV_DESCR)receiveDataBuffer)->bDeviceClass;
			DEBUG_OUT( "Device class %i\n", dv_cls);
			DEBUG_OUT( "Max packet size %i\n", ((PXUSB_DEV_DESCR)receiveDataBuffer)->bMaxPacketSize0);
			// Captured now: receiveDataBuffer is overwritten by the very next
			// control transfer (getConfigurationDescriptor/getDeviceString).
			idVendorL = ((PXUSB_DEV_DESCR)receiveDataBuffer)->idVendorL;
			idVendorH = ((PXUSB_DEV_DESCR)receiveDataBuffer)->idVendorH;
			idProductL = ((PXUSB_DEV_DESCR)receiveDataBuffer)->idProductL;
			idProductH = ((PXUSB_DEV_DESCR)receiveDataBuffer)->idProductH;
			DEBUG_OUT_USB_BUFFER(receiveDataBuffer);
			addr = rootHubIndex + ((PUSB_SETUP_REQ)SetUSBAddressRequest)->wValueL; //todo wValue always 2.. does another id work?
			s = setUsbAddress(addr);
			if ( s == ERR_SUCCESS )
			{
				rootHubDevice[rootHubIndex].address = addr;
				if (dv_cls == USB_DEV_CLASS_HUB)
				{
					// External hub on this root port: hand off to the hub
					// driver, which enumerates each downstream port using
					// its own address space (HUB_PORT_ADDR_BASE+) and
					// never touches HIDdevice[]/CDCdevice[] the way a
					// directly-connected device does at this address.
					s = initializeHub(rootHubIndex, addr);
					if (s == ERR_SUCCESS)
						return ERR_SUCCESS;
				}
				else
				{
					s = enumerateDeviceAtAddress(rootHubIndex, HUB_PORT_DIRECT, addr, idVendorL, idVendorH, idProductL, idProductH);
					if (s == ERR_SUCCESS)
						return ERR_SUCCESS;
				}
			}
		}
		DEBUG_OUT( "Error = %02X\n", s);
		sendProtocolMSG(MSG_TYPE_ERROR,0, rootHubIndex+1, s, 0xEE, 0);
		rootHubDevice[rootHubIndex].status = ROOT_DEVICE_FAILED;
		setUsbSpeed(1);	//TODO define speeds
	}
	return s;
}

unsigned char checkRootHubConnections()
{
	unsigned char s;
	s = ERR_SUCCESS;
	if (UIF_DETECT)                                                        
	{
		UIF_DETECT = 0;    
			if(USB_HUB_ST & bUHS_H0_ATTACH)
			{
				if(rootHubDevice[0].status == ROOT_DEVICE_DISCONNECT || (UHUB0_CTRL & bUH_PORT_EN) == 0x00)
				{
					disableRootHubPort(0);	//todo really need to reset register?
					rootHubDevice[0].status = ROOT_DEVICE_CONNECTED;
					DEBUG_OUT("Device at root hub %i connected\n", 0);
					sendProtocolMSG(MSG_TYPE_CONNECTED,0, 0x01, 0x01, 0x01, 0);
					s = initializeRootHubConnection(0);
				}
			}
			else
			if(rootHubDevice[0].status >= ROOT_DEVICE_CONNECTED)
			{
    			resetHubDevices(0);
				disableRootHubPort(0);
				DEBUG_OUT("Device at root hub %i disconnected\n", 0);
					sendProtocolMSG(MSG_TYPE_DISCONNECTED,0, 0x01, 0x01, 0x01, 0);
				s = ERR_USB_DISCON;
			}
			if(USB_HUB_ST & bUHS_H1_ATTACH)
			{
				
				if(rootHubDevice[1].status == ROOT_DEVICE_DISCONNECT || (UHUB1_CTRL & bUH_PORT_EN) == 0x00)
				{
					disableRootHubPort(1);	//todo really need to reset register?
					rootHubDevice[1].status = ROOT_DEVICE_CONNECTED;
					DEBUG_OUT("Device at root hub %i connected\n", 1);
					sendProtocolMSG(MSG_TYPE_CONNECTED,0, 0x02, 0x02, 0x02, 0);
					s = initializeRootHubConnection(1);
				}
			}
			else
			if(rootHubDevice[1].status >= ROOT_DEVICE_CONNECTED)
			{
    			resetHubDevices(1);
				disableRootHubPort(1);
				DEBUG_OUT("Device at root hub %i disconnected\n", 1);
					sendProtocolMSG(MSG_TYPE_DISCONNECTED,0, 0x02, 0x02, 0x02, 0);
				s = ERR_USB_DISCON;
			}
	}
	return s;
}