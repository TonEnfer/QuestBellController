/*************** Функция "слушателя" ***************/
/*На вход принимает uint16_t dest_port - порт, НА КОТОРЫЙ пришёл пакет
                    uint8_t src_ip[IP_LEN] - IP адрес, с которого пришел пакет
                    uint16_t src_port - порт, С КОТОРОГО пришел пакет
                    const char *data - данные в пакете
                    uint16_t len - длина данных */
void udpListen(uint16_t dest_port, uint8_t src_ip[IP_LEN], uint16_t src_port, const char *data, uint16_t len)
{
  /*************** сохранение IP, с которого пришёл пакет, в качестве IP ПК ***************/
  if (PC_ip[3] != src_ip[3] && src_ip[3] != myip[3])                              //Если адрес, с которого пришёл пакет, не равен уже сохраненному и не равен адресу ардуины
    for (uint8_t i = 0; i < IP_LEN; i++)                                          //сохраняем его
      PC_ip[i] = src_ip[i];
  /*************************************/

  /*************** сохранение порта, с которого пришёл пакет, в качестве порта ПК ***************/
  if (PC_port != src_port)                                                        //Если порт, с которого пришёл пакет, не равен уже сохраненному
    PC_port = src_port;                                                           //сохраняем его
  /*************************************/

  /*************** сохранение порта, на который пришёл пакет, в качестве порта ардуины ***************/
  //Какая-то бестолковая штука. Похоже, что будет работать и без этого. Не знаю, зачем я это писал
  if (myPort != dest_port)
    myPort = dest_port;
  /*************************************/

  /*************** дебажный вывод ***************/
  /*Печатает содержимое пакета, IP и порт, с которого он пришёл*/
#ifdef DEBUG_PRINT
  Serial.print(F("Receive package. Package data: "));
  for (uint16_t i = 0; i < len; i++)
  {
    Serial.print(data[i], HEX);
  }
  Serial.print(F(", receive from "));
  ether.printIp(src_ip);
  Serial.print(":");
  Serial.println(src_port);
#endif
  /*************************************/

  /*************** проверка адреса, с которого пришёл пакет ***************/
  //WTF???
  //Странный блок, назначение которого для меня не совсем ясно.
  //Вроде как можно сразу отвечать на PC_ip[], не записывая его в tmp_ip[]
  //Но это не точно
  uint8_t tmp_ip[4] = {0, 0, 0, 0};                                             // адрес, на который будем отвечать
  if (src_ip[3] != myip[3])                                                     // если не равен адресу ардуины, то отвечаем на него
    for (uint8_t i = 0; i < 4; i++)
      tmp_ip[i] = src_ip[i];
  else                                                                          // иначе отвечаем на адрес, записанный в PC_ip[]
    for (uint8_t i = 0; i < 4; i++)
      tmp_ip[i] = PC_ip[i];
  /*************************************/

  /*************** Парсинг посылки и ответ ПК ***************/
  /*!!!!!!!!!!!!!!! Почти вся магия тут !!!!!!!!!!!!!!!*/
  if (sizeof(data) / sizeof(char) != 0) {                                        // Проверяем, что пакет не пустой.
                                                                                 // Не понятно, зачем считать, если на вход функции подаётся длина
    if (!data[1])                                                               // Если второй бит в посылке (Чтение\запись) равен 0, т.е. ПК хочет прочитать данные
      switch ((uint8_t)data[0])                                                 // Проверяем, данные для каких переферийных устройств ему нужны
      {
        case (0):                                                               //Если 0, то ПК хочет данные от всех устройств. Отправим их
          update_PC_data_bool_type(1, bellHanging, dest_port, tmp_ip, src_port);
          update_PC_data_bool_type(2, bellRinging, dest_port, tmp_ip, src_port);
          update_PC_data_bool_type(3, outOn, dest_port, tmp_ip, src_port);
          update_PC_data_bool_type(254, ended, dest_port, tmp_ip, src_port);
          break;
        case (1):                                                               //Если 1, то ПК хочет данные bellHanging. Отправим их
          update_PC_data_bool_type(1, bellHanging, dest_port, tmp_ip, src_port);
          break;
        case (2):                                                               //Если 2, то ПК хочет данные bellRinging. Отправим их
          update_PC_data_bool_type(2, bellRinging, dest_port, tmp_ip, src_port);
          break;
          case (3):                                                               //Если 2, то ПК хочет данные bellRinging. Отправим их
          update_PC_data_bool_type(3, outOn, dest_port, tmp_ip, src_port);
          break;
        case (254):                                                           //Если 254, то ПК хочет данные ended. Отправим их
          update_PC_data_bool_type(254, ended, dest_port, tmp_ip, src_port);
          break;
        default: break;
      }
    else                                                                        //ПК хочет записать данные
      if (data[0] == 3)                                                         //Если хочет записывать в bellRinging
      {
#ifdef DEBUG_PRINT                                                              //Дебажный вывод
        Serial.print("Out switch to ");
        Serial.println((uint8_t)data[4], HEX);
#endif
        outOn = (uint8_t)data[4];                                                //Записываем данные из 5 бита (должно быть приведение к типу bool, но и так норм)
        if (outOn) forceOutOn = true; else forceOutOn = false;                        //Если замок закрыт, значит оператор его не открывал => forceOpened = false
        switchOutState();                                                      //Изменяем состояние замка в соответствии с новыми флагами
      }
    /*************************************/
  }
}
/*************************************/

/*************** функция обновления данных на ПК ***************/
/*На вход принимает uint16_t src_port - порт, С КОТОРОГО отправлять пакет
                    uint8_t dst_ip[IP_LEN] - IP адрес, НА КОТОРЫЙ отправлять пакет
                    uint16_t dst_port - порт, НА КОТОРЫЙ отправлять пакет */
/*Вызывается из основного цикла для принудительного обновления данных на ПК.
  Отправляет пакеты только если флаги изменились. Тем самым обеспечивается низкая загруженность сети*/
void updatePCdata(uint16_t src_port, uint8_t dst_ip[IP_LEN], uint16_t dst_port)
{
  /*************** переменные для хранения предыдущего состояния флагов ***************/
  static bool bellHanging_old;
  static bool bellRinging_old;
  static bool outOn_old;
  static bool ended_old;
  /*************************************/

  /*Сравниваем предыдущее состояние и новое. Если состояние изменилось, отправляем пакет с новым состоянием*/
  if (ended_old != ended)
  {
    update_PC_data_bool_type(254, ended, src_port, dst_ip, dst_port);
    ended_old = ended;
  }
  /*Сравниваем предыдущее состояние и новое. Если состояние изменилось, отправляем пакет с новым состоянием*/
  if (bellHanging_old != bellHanging)
  {
    update_PC_data_bool_type(1, bellHanging, src_port, dst_ip, dst_port);
    bellHanging_old = bellHanging;
  }
  /*Сравниваем предыдущее состояние и новое. Если состояние изменилось, отправляем пакет с новым состоянием*/
  if (bellRinging_old != bellRinging)
  {
    update_PC_data_bool_type(2, bellRinging, src_port, dst_ip, dst_port);
    bellRinging_old = bellRinging;
  }
  if (outOn_old != outOn)
  {
    update_PC_data_bool_type(3, outOn, src_port, dst_ip, dst_port);
    outOn_old = outOn;
  }
}
/*************************************/

/*************** функция отправки на ПК пакета с типом данных bool ***************/
/*На вход принимает uint8_t per_addr - адрес переферийного устройства
                    bool data - данные
                    uint16_t src_port - порт, С КОТОРОГО отправлять пакет
                    uint8_t dst_ip[IP_LEN] - IP адрес, НА КОТОРЫЙ отправлять пакет
                    uint16_t dst_port - порт, НА КОТОРЫЙ отправлять пакет */
void update_PC_data_bool_type(uint8_t per_addr, bool data, uint16_t src_port, uint8_t dst_ip[IP_LEN], uint16_t dst_port)
{
  /*************** формирование пакета в соответствии с форматом ***************/
  uint8_t length = 5;                                                                   //Длина пакета

  /*************** создание и иниализация массива ***************/
  //По идее, вместо всей этой конструкции можно использовать void * calloc( size_t number, size_t size )
  char tmp[length];
  for (uint16_t i = 0; i < sizeof(tmp) / sizeof(char); i++)
  {
    tmp[i] = 0;
  }
  /*************************************/

  /*************** Заполнение массива нужными данными ***************/
  /*
    1 байт - адрес переферийного устройства
    2 байт - не используется
    3 байт - не используется
    4 байт - длина данных
    5 байт - данные
  */
  tmp[0] = per_addr;
  tmp[1] = 0x01;
  tmp[2] = 0x01;
  tmp[3] = sizeof(data);
  tmp[4] = data;
  /*************************************/

  /*************** Дебажный вывод ***************/
#ifdef DEBUG_PRINT
  Serial.print(F("Send package. Package data: "));
  for (uint16_t i = 0; i < length ; i++)
  {
    Serial.print((uint8_t)tmp[i], HEX);
  }
  Serial.print(", send to ");
  ether.printIp(dst_ip);
  Serial.print(":");
  Serial.print(dst_port);
  Serial.println();
#endif
  /*************************************/

  /*************** Отправка пакета ***************/
  while (ether.clientWaitingGw())                                                       //Не помню, зачем, но видимо надо :D
    ether.packetLoop(ether.packetReceive());
  ether.sendUdp(tmp, length, src_port , dst_ip, dst_port );                             //Отправка пакета.
}
