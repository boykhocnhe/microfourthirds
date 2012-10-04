/* fakelens.cpp
 * Code that pretends to be a lens and talks to the body.
 * Steven Bell <sebell@stanford.edu>
 * 27 September 2012
 */

#include "Arduino.h"
#include "typedef.h"
#include "common.h"

/* Performs one-time pin initialization and other setup.  The pin directions
 * here are the opposite of fakebody, since we're playing the other side. */
void setup() {
  Serial.begin(115200);
  pinMode(SLEEP, INPUT);
  pinMode(BODY_ACK, INPUT);
  pinMode(LENS_ACK, OUTPUT);
  pinMode(FOCUS, INPUT);
  pinMode(SHUTTER, INPUT);

  // Configure the SPI hardware
  // SPE - Enable
  // DORD - Set data order to LSB-first
  // Slave mode (Master bit is not set)
  // CPOL - Set clock polarity to "normally high"
  // CPHA - Set to read on trailing (rising) edge
  SPCR = (1<<SPE) | (1<<DORD) | (1<<CPOL) | (1<<CPHA);

  // Set up the SPI pins
  pinMode(CLK, INPUT);
  pinMode(DATA_MISO, INPUT); // Until we have an explicit write, make both inputs
  pinMode(DATA_MOSI, INPUT);
}
// Wait for a falling edge on the body ACK pin
inline void waitBodyFall()
{
  while(!(BODY_ACK_PIN & BODY_ACK_HIGH)){} // Wait until it's high first
  while(BODY_ACK_PIN & BODY_ACK_HIGH){}
}

// Wait for a rising edge on the body ACK pin
inline void waitBodyRise()
{
  while(BODY_ACK_PIN & BODY_ACK_HIGH){} // Wait until it's low first
  while(!(BODY_ACK_PIN & BODY_ACK_HIGH)){}
}

// Wait until the body ACK pin is low
inline void waitBodyLow()
{
  delayMicroseconds(2);
  while(BODY_ACK_PIN & BODY_ACK_HIGH){}
}

// Wait until the body ACK pin is high
inline void waitBodyHigh()
{
  delayMicroseconds(2);
  while(!(BODY_ACK_PIN & BODY_ACK_HIGH)){}
}

/* Reads a single byte from the SPI bus.
 * Data is read LSB-first
 */
uint8 readByte()
{
  pinMode(DATA_MISO, INPUT); // Just in case it was an output last

  // Clear the SPIF bit from any previously received bytes by reading SPDR
  SPDR = 0x00;

  // Wait until we receive a byte
  while(!(SPSR & (1<<SPIF))) {}

  return(SPDR);
}

/* Writes an 8-bit value on the data bus.  The clock is driven by the body.
 */
void writeByte(uint8 value)
{
  // Set the bytes we want to write
  SPDR = value;

  // Set the MISO pin to be an output
  //DATA_PIN |= DATA_MISO_WRITE;
  pinMode(DATA_MISO, OUTPUT);

  // Wait until transmission is finished
  while(!(SPSR & (1<<SPIF))) {}

  // Clear SPIF
  // BUG: When this was set to 0x00, it didn't do anything.  Perhaps it was optimized away?
  SPDR = 0xFF;
}

/* Reads a number of bytes and then transmits the checksum.
 * nBytes - Number of bytes to read.  Must be greater than zero.
 */
void readBytesChecksum(uint8 nBytes)
{
  uint8 checksum = 0;

  for(uint8 i = 0; i < nBytes - 1; i++){
    checksum += readByte();
    digitalWrite(LENS_ACK, 0); // Working
    digitalWrite(LENS_ACK, 1); // Ready
  }

  // Last byte
  checksum += readByte(); // 0x00
  digitalWrite(LENS_ACK, 0); // Working
  // Note: No ready here, we're waiting for the body to drop

  // Now we reply with the checksum
  //pinMode(DATA, OUTPUT);
  //digitalWrite(DATA, HIGH); // Not sure why this is necessary, but it is
  waitBodyFall();
  digitalWrite(LENS_ACK, 1); // Ready
  waitBodyHigh();
  writeByte(checksum);
}

// # bytes, bytes, checksum
void writeBytesChecksum(uint8 nBytes, uint8* values)
{
  uint8 checksum = 0;

  // Write the first byte, which is the number of bytes in the packet
  waitBodyFall(); // Wait for body to drop
  digitalWrite(LENS_ACK, 0); // We drop and then rise (ready to send next byte)
  digitalWrite(LENS_ACK, 1);
  writeByte(nBytes);

  // Now write the byte values themselves
  for(uint8 i = 0; i < nBytes; i++){
    waitBodyLow();
    digitalWrite(LENS_ACK, 0);
    digitalWrite(LENS_ACK, 1);
    writeByte(values[i]);
    checksum += values[i]; // Keep a running total
  }

  // Finally, write the checksum
  waitBodyLow();
  digitalWrite(LENS_ACK, 0);
  digitalWrite(LENS_ACK, 1);
  writeByte(checksum);
}

void standbyPacket(void)
{
  readBytesChecksum(4);

  uint8 values[31] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  writeBytesChecksum(31, values);
}

int main()
{
  init(); // Arduino library init
  setup(); // Pin setup

  // Sit and wait for the sleep pin to go high (camera is turned on)
  while(digitalRead(SLEEP) == 0){}

  // Check that the body ACK pin is high
  waitBodyHigh();

  // Pulse our ACK pin to let the body know we're awake
  digitalWrite(LENS_ACK, 1);
  delay(10);
  digitalWrite(LENS_ACK, 0);

  // Wait until the body ACK goes high
  waitBodyRise();

  // Ready
  digitalWrite(LENS_ACK, 1);

  readBytesChecksum(4); // Read four bytes

  waitBodyFall();
  digitalWrite(LENS_ACK, 0);
  waitBodyRise();
  digitalWrite(LENS_ACK, 1);

  delay(500);

  digitalWrite(LENS_ACK, 0);
  waitBodyLow(); // Falling edge happens very fast
  digitalWrite(LENS_ACK, 1);
  waitBodyRise();

  writeByte(0x00);

  waitBodyLow(); // Falling edge happens very fast
  digitalWrite(LENS_ACK, 0);
  waitBodyRise();
  digitalWrite(LENS_ACK, 1);

  readBytesChecksum(4); // Read four bytes

  uint8 sendBytes[5] = {0x00, 0x0a, 0x10, 0xc4, 0x09};
  writeBytesChecksum(5, sendBytes);

  waitBodyLow(); // Drop happens very fast
  digitalWrite(LENS_ACK, 0);
  waitBodyHigh();
  digitalWrite(LENS_ACK, 1);

  readBytesChecksum(4);

  waitBodyLow();
  digitalWrite(LENS_ACK, 0);
  waitBodyHigh();
  digitalWrite(LENS_ACK, 1);

  // The body drops the clock for some unknown reason, ruining the
  // SPI line synchronization.  Reset the hardware to fix it.
  SPCR = 0;
  SPCR = (1<<SPE) | (1<<DORD) | (1<<CPOL) | (1<<CPHA);
  readBytesChecksum(4);

  // Information contained in here:
  // Aperture limits, focus limits, zoom?
  // Firmware version
  // Vendor
  // # bytes, bytes, checksum
  uint8 sendBytes2[21] = {0x00, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x41,
                          0x41, 0x41, 0x32, 0x34, 0x33, 0x38, 0x34, 0x31,
                          0x00, 0x00, 0x00, 0x01, 0x11};
  writeBytesChecksum(21, sendBytes2);

  waitBodyLow();
  digitalWrite(LENS_ACK, 0);
  waitBodyHigh();
  digitalWrite(LENS_ACK, 1);

  readBytesChecksum(4);

  // The body expects some bytes here...
  writeBytesChecksum(2, sendBytes2);

  waitBodyLow();
  digitalWrite(LENS_ACK, 0);
  waitBodyHigh();
  digitalWrite(LENS_ACK, 1);

  readBytesChecksum(4);

  waitBodyLow();
  digitalWrite(LENS_ACK, 0);
  delay(10);
  digitalWrite(LENS_ACK, 1);

  while(1){
    //standbyPacket();
  }
  return(0);
}
