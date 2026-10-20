void setup()
{
    Serial1.begin(115200, SERIAL_8N1, 20, 21);
}

void loop()
{
    Serial1.write('T');
    delay(1000);
}
