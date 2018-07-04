#include "HX711.h"
#include <EtherCard.h>
#include <IPAddress.h>

#define BELL_CLOCK 500                                              //Частота звучания колокола
#define PERIODS 8                                                   //Количество периодов, которые нужно считать
#define POINTS_PER_PERIOD 16                                        //Количество точек в периоде
#define TIMER_PERIOD F_CPU/(BELL_CLOCK*POINTS_PER_PERIOD)           //Количество тиков таймера до сброса
#define ARRAY_SIZE POINTS_PER_PERIOD*PERIODS                        //Размер массива для хранения данных
#define AMP_THRESHOLD 1000000UL                                     //Порог срабатывания (амплитуда сигнала на частоте звучания колокола)

#define HX711_DOUT 5                                                //Пин данных для HX711
#define HX711_SCK 4                                                 //Пин тактирования для HX711
#define HX711_COLIBRATION_COEFFICIENT 16.4f                         //Колибровочный коэффициент для HX711
#define WEIGHT_THRESHOLD 3.500                                      //Порог срабатывания (вес)

#define OUT 9                                                       //Выходной пин
#define ETH_SS_PIN 10                                               //Slave Select для ENC28J60

float alpha = 0.3;
float fWeight = 0;
HX711 scale1(HX711_DOUT, HX711_SCK);

volatile bool putADC = false;                           //Флажок для работы АЦП. При заполнении массива устанавливается в false.
volatile bool bellHanging = false;                      //Колокол висит
volatile bool bellRinging = false;                      //Колокол звенит
volatile bool outOn = false;                            //Выход включен
volatile bool forceOutOn = false;
volatile bool ended = false;                            //Взаимодействие завершено

bool etnOn = false;                                     //ENC28J60 включен

const uint8_t valuesSize = ARRAY_SIZE;                  //Размер массива данных. Не изменять, опасно, убьёт. Анализ построен на фиксированной длине массива
volatile int16_t values[valuesSize];                    //Массив данных
volatile uint8_t pointer = 0;                           //Указатель в массиве данных

//Вспомогательные таблицы синуса и косинуса
int8_t sinus[] = {0, 48, 89, 117, 127, 117, 89, 48, 0, -49, -90, -118, -127, -118, -90, -49};
int8_t cosinus[] = {127, 117, 89, 48, 0, -49, -90, -118, -127, -118, -90, -49, -1, 48, 89, 117};

#define STATIC 1                                              // Адрес статический или получаемый по DHCP
#define DEBUG_PRINT                                           // Если нужен дебажный вывод, раскомментировать
byte Ethernet::buffer[500];                                   // буфер для приёма и передачи данных по tcp/ip

static byte mymac[] = { 0x70, 0x69, 0x69, 0x2D, 0x30, 0x03 }; //mac-адрес ардуины. 2 уровень модели OSI

#if STATIC
// ethernet interface ip address
static byte myip[] = { 192, 168, 250, 192 };                  //Адрес ардуины
static byte mask[] = {255, 255, 255, 0};                      //Маска подсети
static byte gwip[] = { 192, 168, 250, 1 };                    //Адрес шлюза
static byte dns_ip[] = {192, 168, 250, 1};                    //Адрес DNS сервера
#endif

static uint8_t PC_ip[IP_LEN] = {0, 0, 0, 0};                  //Адрес компьютера, на котором запущена управляющая программа
//Здесь не указывается, а только создаётся переменнная
static uint16_t PC_port = 0;                                  //Порт, с которого ПК отправляет пакеты и на который нужно отвечать
//Здесь не указывается, а только создаётся переменнная
static uint16_t myPort = 1024;                                //Порт, который ардуина "слушает" для получения пакетов от ПК

void ethConf()
{
  if (ether.begin(sizeof Ethernet::buffer, mymac, 10) == 0) {              //Запуск сетевой карты. В случае ошибки выполняется условие
    Serial.println(F("Failed to access Ethernet controller"));
  }
  else {
    etnOn = true;
    Serial.println(F("Eth controller begining"));
#if STATIC                                                                //Если выше указано, что используются статические адреса
    ether.staticSetup(myip, gwip, dns_ip, mask);                            //сетевая карта запускается с указанными выше реквизитами
#else                                                                     //иначе
    if (!ether.dhcpSetup())                                                 //попытка получить настройки по DHCP. В случае ошибки выполнится условие
#ifdef DEBUG_PRINT
      Serial.println(F("DHCP failed"));
#endif
#endif
#ifdef DEBUG_PRINT                                              //Если включен дебажный вывод, 
    ether.printIp("IP:  ", ether.myip);                           //печатаются
    ether.printIp("GW:  ", ether.gwip);                           //установленные
    ether.printIp("DNS: ", ether.dnsip);                          //настройки
    ether.printIp("NetMask: ", ether.netmask);                    //сетевой карты
#endif
    /*!!!!!!!!!!!!!!!!!!!!!Важное место!!!!!!!!!!!!!!!!!!!!!!!!!*/
    ether.udpServerListenOnPort(&udpListen, myPort);              //Создаётся "слушатель" для порта. Если на порт myPort приходит пакет, то выполняется функция udpListen
#ifdef DEBUG_PRINT                                              //Если включен дебажный вывод, печатается сообщение, что "слушатель" добавлен
    Serial.print(F("udpServerListenOnPort "));
    Serial.print(myPort);
    Serial.println(" added");
#endif
    while (ether.clientWaitingGw())                               //Не помню, зачем, но видимо надо :D
      ether.packetLoop(ether.packetReceive());
    Serial.println("Go");
  }
}

//Функция настройки таймера
inline void adcTimerConfig() {
  TCCR1A = 0;

  uint8_t wavegen =  _BV(WGM12);// CTC mode OCR1A
  TCCR1B = wavegen;

  OCR1A = TIMER_PERIOD;
  OCR1B = 1;
}

//Функция запуска таймера
inline void timer_start() {
  TCCR1B |= _BV(CS10);  //no prescaler - 16MHz
}

//Функция остановки таймера
inline void timer_stop() {
  TCCR1B &= ~(_BV(CS10) | _BV(CS11) | _BV(CS12));
}
//Функция настройки АЦП
inline void adcConfig() {
  uint8_t refs = _BV(REFS0);
  uint8_t adlar = 0;
  uint8_t mux = 0; //ADC0
  ADMUX = refs | mux | adlar;

  uint8_t enable = _BV(ADEN);
  uint8_t auto_trigger = _BV(ADATE);
  uint8_t interrupt = _BV(ADIE);
  uint8_t prescale = _BV(ADPS2) | _BV(ADPS0); //prescaler 32 - 500kHz
  ADCSRA = enable | auto_trigger | interrupt | prescale;

  uint8_t trigSource = _BV(ADTS2) | _BV(ADTS0); //Trigger Source - Timer/Counter1 Compare Match B
  ADCSRB = trigSource;
}

//Прерывание АЦП.
//Если массив не заполнен, записывает в него данные. Иначе устанавливает флаг окончания записи
ISR(ADC_vect) {
  //PB5
  TIFR1 |= _BV(OCF1B);
  if (!putADC)
    return;
  if (pointer == valuesSize) {
    putADC = false;
    pointer = 0;
    timer_stop();
    return;
  }
  values[pointer] = (int16_t)ADC;
  pointer++;
  return;
}

//Функция, призывающая магию преобразования Фурье
//Делает свёртку
//Возвращает значение комплексной амплитуды (амплитуда + фаза)
uint64_t getFourierMagic()
{
  int32_t sumsin = 0;
  int32_t sumcos = 0;
  for (int i = 0; i < valuesSize; i++) {
    int32_t curval = values[i];
    sumsin += ((int32_t)sinus[i % 16]) * curval;
    sumcos += ((int32_t)cosinus[i % 16]) * curval;
  }
  int64_t sum = abs(sumsin) + abs(sumcos);
  Serial.println((int32_t)sum, DEC);
  return sum;
}

//Функция, проверяющая, звенит ли колокол
bool isBellRinging() {
  uint64_t sum = getFourierMagic();
  if (sum > AMP_THRESHOLD)
    return true;
  return false;
}

void hx711Config(float coeff)
{
  scale1.set_scale(coeff);
  scale1.tare();
}

void hx711Meas()
{
  float  w = scale1.get_units(5) / 1000;
  fWeight = w * alpha + (fWeight * (1 - alpha));
  Serial.println(fWeight);
}

bool isBellHanging()
{
  if ((fWeight < WEIGHT_THRESHOLD + 0.5) &&
      (fWeight > WEIGHT_THRESHOLD - 0.5))
    return true;
  return false;
}

void outConf()
{
  pinMode(OUT, OUTPUT);
  digitalWrite(OUT, LOW);
  outOn = false;
}

void onOut()
{
  digitalWrite(OUT, HIGH);
  outOn = true;
}
void offOut()
{
  digitalWrite(OUT, LOW);
  outOn = false;
}

void switchOutState()
{
  if (bellRinging || forceOutOn)                             //Если зеркало на месте или оператор включил выход
  {
    onOut();                                            //включаем
    outOn = true;
    ended = true;
    //Serial.println("outOn");
  }
  else if (!forceOutOn) {                                 //Иначе,если оператор не включал
    offOut();                                           //выключаем
    outOn = false;
    ended = false;
    //Serial.println("outOff");
  }

}

void setup() {                                          //Настройка железа
  Serial.begin(250000);                                 //UART на максимальной скорости
  delay(1000);                                          //Просто так
  Serial.println("START");                              //Дебаг

  hx711Config(HX711_COLIBRATION_COEFFICIENT);
  ethConf();
  outConf();

  adcTimerConfig();                                     //Настройка таймера для автотриггера АЦП
  adcConfig();                                          //Настройка АЦП
  sei();                                                //Разрешение прерываний
}

void loop() {
  if (etnOn)
    ether.packetLoop(ether.packetReceive());      //Просим сетевую карту получить пакеты

  if (!ended) {
    hx711Meas();
    bellHanging = true;// isBellHanging();
    if (bellHanging)
    {
      putADC = true;
      timer_start();
      while (putADC);
      bellRinging = isBellRinging();
      Serial.println(bellRinging);
    }
    switchOutState();
  }
  if (etnOn)                                                      //Если ethernet включен
    updatePCdata(myPort, PC_ip, PC_port);                         //Обновляем данные на ПК оператора

}
