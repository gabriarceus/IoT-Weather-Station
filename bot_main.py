from typing import List, Optional, Final
from telegram import Update
from telegram.ext import Application, CommandHandler, MessageHandler, filters, ContextTypes
from weather_monitor import reading_queue, reading_lock
from datetime import datetime
import asyncio
import pyrebase


# IMPORTANTE: inserire il token fornito dal BotFather nel file config.py
from config import BOT_TOKEN, BOT_USERNAME, AUTH_USER_ID

# Variabile globale per tenere traccia dell'ultimo momento in cui la temperatura era sotto 15.0
last_time_below_15 = None

last_message_time = None

#Configurazione per Firebase
from config import FIREBASE_WEB_API_KEY, FIREBASE_AUTH_DOMAIN, FIREBASE_DB_URL, FIREBASE_STORAGE_BUCKET
configuration = {
    "apiKey": FIREBASE_WEB_API_KEY,
    "authDomain": FIREBASE_AUTH_DOMAIN,
    "databaseURL": FIREBASE_DB_URL,
    "storageBucket": FIREBASE_STORAGE_BUCKET
}

firebase = pyrebase.initialize_app(configuration)
db = firebase.database()


async def check_temperature(app: Application):
    global last_time_below_15, last_message_time
    while True:
        with reading_lock:
            # Preleva l'ultimo valore di reading senza rimuoverlo dalla coda
            current_reading = reading_queue.queue[0] if not reading_queue.empty() else None

        if current_reading is not None:
            internal_temperature = current_reading[0]
            print(f"Temperatura corrente: {internal_temperature}")  # Aggiunto per il debug
            if internal_temperature < 15.0:
                if last_time_below_15 is None:
                    last_time_below_15 = datetime.now()
                else:
                    elapsed_minutes = (datetime.now() - last_time_below_15).total_seconds() / 60
                    print(f"Temperatura interna sotto i 15.0°C per {int(elapsed_minutes)} minuti")
                    if elapsed_minutes >= 15 and (last_message_time is None or (datetime.now() - last_message_time).total_seconds() >= 5 * 60):  # 5 minuti
                        print("Invio messaggio di allarme")  # Aggiunto per il debug
                        await app.bot.send_message(chat_id=AUTH_USER_ID, text=f"*Attenzione:* la temperatura interna è rimasta sotto 15.0°C da {int(elapsed_minutes)} minuti! 🥶", parse_mode='Markdown')
                        last_message_time = datetime.now()
            else:
                print("Temperatura sopra i 15.0°C")  # Aggiunto per il debug
                last_time_below_15 = None  # Reset del timer se la temperatura è sopra 15.0
                last_message_time = None

        #print(reading_queue.get())
        reading = reading_queue.get()
        data = {
            "Temperatura interna" : reading[0],
            "Umidità interna" : reading[1],
            "Pressione armosferica" : reading[2],
            "Temperatura esterna" : reading[3],
            "Umidità esterna" : reading[4]
        }

        db.child("Status").push(data)

        db.update(data)
        print("Dati inviati a Firebase")

        await asyncio.sleep(30)  # Controlla la temperatura ogni 30 secondi


# Comandi
async def start_command(update: Update, context: ContextTypes.DEFAULT_TYPE):
    if update.message.chat.id == AUTH_USER_ID:
        await update.message.reply_text("Ora sono pronto a darti tutte le informazioni che ti servono sulla mia stanza ☺️\nControlla nel pannello dei comandi per vedere cosa posso fare! 🤭")
    else:
        await update.message.reply_text("Non hai il permesso di usare questo bot 🚫")
        print(f"L'utente ({update.message.chat.id}) non ha i permessi necessari per usare il bot")

async def reading_command(update: Update, context: ContextTypes.DEFAULT_TYPE):
    # Preleva i dati del sensore dalla coda
    reading = reading_queue.get()

    now = datetime.now()
    current_time = now.strftime("%H:%M:%S")
    current_date = now.strftime("%d/%m/%Y")
    
    # Blocca l'accesso alla variabile condivisa
    with reading_lock:
        if update.message.chat.id == AUTH_USER_ID:
            message_values = [
                "Temperatura interna: {} °C",
                "Umidità interna: {} %",
                "Pressione atmosferica: {} hPa",
                "Temperatura esterna: {} °C",
                "Umidità esterna: {} %"
            ]
            combined_message = "\n".join(message.format(value) for message, value in zip(message_values, reading))  # Usa reading direttamente
            print(f"\nMessaggio inviato: ({combined_message})")
            await update.message.reply_text(f"*Valori attuali 🌡️:*\nOra: {current_time}\nData: {current_date}\n\n{combined_message}", parse_mode='Markdown')


        else:
            await update.message.reply_text("Non hai il permesso di usare questo bot 🚫")
            print(f"L'utente ({update.message.chat.id}) non ha i permessi necessari per usare il bot")



# Risposte
def handle_response(text: str) -> str:
    processed: str = text.lower()

    if "misura" in processed:
        return "Se vuoi sapere le misurazioni, usa il comando /reading"

    return "Non ho capito cosa vuoi dire oppure il comando non esiste 😕"

# Messaggi
async def handle_message(update: Update, context: ContextTypes.DEFAULT_TYPE):
    message_type: str = update.message.chat.type
    text: str = update.message.text
    user_id = update.message.chat.id

    print(f'User ({user_id}) in {message_type}: "{text}"')

    if message_type == "group":
        if BOT_USERNAME in text:
            if user_id == AUTH_USER_ID:
                new_text: str = text.replace(BOT_USERNAME, "").strip()
                response = handle_response(new_text)
            else:
                response = "Non disponi dei permessi per poter parlare con me 🚫"
        # Meglio separare gli if così se mi trovo in una chat di gruppo e sono autenticato, ma non ho chiesto esplicitamente al bot di scrivermi non scrive nulla
        else:
            return
    else:
        if user_id == AUTH_USER_ID:
            response: str = handle_response(text)
        else:
            response = "Non disponi dei permessi per poter parlare con me 🚫"

    print("Bot:", response)
    await update.message.reply_text(response)

# Error handler
async def error(update: Update, context: ContextTypes.DEFAULT_TYPE):
    print(f"Update {update} caused error {context.error}")


if __name__ == "__main__":
    print("Starting bot...")
    app = Application.builder().token(BOT_TOKEN).build()

    # Comandi
    app.add_handler(CommandHandler("start", start_command))
    app.add_handler(CommandHandler("reading", reading_command))

    # Messaggi
    app.add_handler(MessageHandler(filters.TEXT, handle_message))

    # Errors
    app.add_error_handler(error)

    # Avvia il task per controllare la temperatura
    loop = asyncio.get_event_loop()
    loop.create_task(check_temperature(app))


    # Polling al bot
    print("Polling...")
    app.run_polling(poll_interval=3)
