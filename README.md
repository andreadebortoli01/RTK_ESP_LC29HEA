# ESP32 RTK GNSS Logger & Webserver

Questo progetto permette di utilizzare una scheda **ESP32** come ricevitore **GNSS RTK**, con:

- Logging dei dati  
- Invio NMEA a caster **NTRIP**  
- Visualizzazione web in tempo reale  
- Monitoraggio dei sensori **IMU**

---

## 🔧 Funzionalità

- ✅ Connessione WiFi automatica con credenziali configurabili  
- 🌐 Connessione a caster NTRIP per correzioni RTK (configurabile)  
- 🛰️ Parsing e logging dei messaggi NMEA (`GGA`, `RMC`, `PQTMEPE`)  
- 📡 Invio periodico di messaggi `GGA` al caster NTRIP  
- 🌍 **Webserver integrato** su porta 80:
  - Pagina HTML con:
    - Mappa
    - Stato GNSS
    - Dati IMU
    - Log e grafici
  - API REST `/api` per ottenere i dati in formato JSON  
- 📈 Monitoraggio **IMU** (accelerometro e giroscopio)  
- ⚙️ Calcolo e visualizzazione del **CPU usage medio**  
- 🔄 Gestione di **buffer circolari** per log e dati RTCM  

---

## 🛠️ Hardware richiesto

- ESP32 Dev Board  
- Modulo GNSS compatibile (es. **Quectel LC29HEA**)  
- Modulo IMU (es. **MPU9250**)  
- Connessione WiFi
