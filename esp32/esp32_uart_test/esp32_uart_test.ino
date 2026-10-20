void setup()
{
    Serial.begin(115200);
    Serial1.begin(115200, SERIAL_8N1, 20, 21);
}

void loop()
{
    Serial1.write('T');
    Serial.println("Sent: T");

    unsigned long start = millis();

    while (Serial1.available() == 0 && millis() - start < 500)
    {
        // Wait up to 500 milliseconds for a reply.
    }

    if (Serial1.available() > 0)
    {
        char reply = Serial1.read();

        Serial.print("Received: ");
        Serial.println(reply);
    }
    else
    {
        Serial.println("No reply");
    }

    delay(1000);
}
