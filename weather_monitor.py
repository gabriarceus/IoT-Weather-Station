from influxdb_client import InfluxDBClient, Point, WriteOptions
from influxdb_client.client.write_api import SYNCHRONOUS
import serial
import threading
import queue
from config import DB_TOKEN

DEVICE_NAME="ESP32"
BAUDRATE=9600

#print('Press Ctrl+Z to stop and exit!')

#serial_port=input("Enter Serial Port #: ")

#ser = serial.Serial("/dev/tty%s"%(serial_port), BAUDRATE, timeout=5)
ser = serial.Serial("/dev/ttyUSB0", BAUDRATE, timeout=5)
ser.baudrate = BAUDRATE

client = InfluxDBClient(url="http://localhost:8086", token= DB_TOKEN, org="IoT_Database", bucket="weatherdb")
write_api = client.write_api(write_options=SYNCHRONOUS)

# Crea un nuovo oggetto Lock
reading_lock = threading.Lock()

# Crea una variabile globale per memorizzare i dati del sensore
reading = [0, 0, 0, 0, 0, 0]

reading_queue = queue.Queue()

def read_sensor_data():
    global reading
    while True:
        line = ser.readline()
        try:
            line = line.decode()  # Decodifica i dati in una stringa
            fields = line.strip().split(',')

            if len(fields) == 6:
                # Aggiorna la variabile reading con i nuovi dati del sensore
                reading = [float(field) for field in fields]

                # Svuota la coda prima di mettere i nuovi dati del sensore, così ho sempre i dati più aggiornati
                with reading_lock:
                    while not reading_queue.empty():
                        reading_queue.get()
                    reading_queue.put(reading)

                print('I. Temperature: %s, I. Humidity: %s, Pressure: %s, E. Temperature: %s, E. Humidity: %s, CO2: %s' % tuple(reading))

                it = Point("temperature").tag("source", DEVICE_NAME).field("value", reading[0])
                write_api.write(bucket="weatherdb", org="IoT_Database", record=it)
                ih = Point("humidity").tag("source", DEVICE_NAME).field("value", reading[1])
                write_api.write(bucket="weatherdb", org="IoT_Database", record=ih)
                p = Point("pressure").tag("source", DEVICE_NAME).field("value", reading[2])
                write_api.write(bucket="weatherdb", org="IoT_Database", record=p)
                et = Point("Etemperature").tag("source", DEVICE_NAME).field("value", reading[3])
                write_api.write(bucket="weatherdb", org="IoT_Database", record=et)
                eh = Point("Ehumidity").tag("source", DEVICE_NAME).field("value", reading[4])
                write_api.write(bucket="weatherdb", org="IoT_Database", record=eh)
                Co2 = Point("CO2").tag("source", DEVICE_NAME).field("value", reading[5])
                write_api.write(bucket="weatherdb", org="IoT_Database", record=Co2)

        except UnicodeDecodeError:
            print("Errore di decodifica: La riga non è una stringa valida.")

# Avvia un nuovo thread per eseguire la funzione read_sensor_data
reading_thread = threading.Thread(target=read_sensor_data)
reading_thread.start()