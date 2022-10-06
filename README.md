# REST API towards Micronova Stoves

The captive portal has a menu item containing the API documentation

<img src="/img/settings.png" width="200">

# Hardware

I'm using a ESP32 dev board on a custom PCB:

<img src="/img/pcb.png" width="200">

It's using N-Channel MOSFETs to convert the 3.3v TTL towards the 5V TTL the stove understands.
Micronova uses RS232 but TX and RX are coupled together.

This means whathever the ESP is transmittig is also received.
In software we remove the same bits from the received bits in order to have what we need.


 

