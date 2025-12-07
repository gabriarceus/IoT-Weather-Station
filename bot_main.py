from typing import List, Optional, Final
from telegram import Update
from telegram.ext import Application, CommandHandler, MessageHandler, filters, ContextTypes, ConversationHandler
from weather_monitor import reading_queue, reading_lock
from datetime import datetime
import asyncio
import pyrebase


# IMPORTANT: insert the token provided by BotFather in the config.py file
from config import BOT_TOKEN, BOT_USERNAME, AUTH_USER_ID

# Global variable to keep track of the last time the temperature was below 14.0
last_time_below_14 = None

last_message_time = None

# Firebase Configuration
from config import FIREBASE_WEB_API_KEY, FIREBASE_AUTH_DOMAIN, FIREBASE_DB_URL, FIREBASE_STORAGE_BUCKET
configuration = {
    "apiKey": FIREBASE_WEB_API_KEY,
    "authDomain": FIREBASE_AUTH_DOMAIN,
    "databaseURL": FIREBASE_DB_URL,
    "storageBucket": FIREBASE_STORAGE_BUCKET
}

#firebase = pyrebase.initialize_app(configuration)
#db = firebase.database()

# Conversation states
WAITING_FOR_TEMPERATURE = range(1)


async def check_temperature(app: Application):
    global last_time_below_14, last_message_time
    while True:
        with reading_lock:
            # Retrieve the last reading value without removing it from the queue
            current_reading = reading_queue.queue[0] if not reading_queue.empty() else None

        if current_reading is not None:
            internal_temperature = current_reading[0]
            if internal_temperature < 14.0:
                if last_time_below_14 is None:
                    last_time_below_14 = datetime.now()
                else:
                    elapsed_minutes = (datetime.now() - last_time_below_14).total_seconds() / 60
                    print(f"Temperatura interna sotto i 14.0°C per {int(elapsed_minutes)} minuti")
                    if elapsed_minutes >= 15 and (last_message_time is None or (datetime.now() - last_message_time).total_seconds() >= 5 * 60):  # 5 minutes
                        print(f"\nInvio messaggio di allarme\nTemperatura interna pari a {int(internal_temperature)}\n")  # Added for debugging
                        await app.bot.send_message(chat_id=AUTH_USER_ID, text=f"*Attenzione:* la temperatura interna è rimasta sotto 14.0°C da {int(elapsed_minutes)} minuti! 🥶", parse_mode='Markdown')
                        last_message_time = datetime.now()
            else:
                last_time_below_14 = None  # Reset timer if temperature is above 14.0
                last_message_time = None
        '''
        # OLD FIREBASE CONFIG
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
        '''
        await asyncio.sleep(30)  # Check temperature every 30 seconds


# Commands
async def start_command(update: Update, context: ContextTypes.DEFAULT_TYPE):
    if update.message.chat.id == AUTH_USER_ID:
        await update.message.reply_text("Ora sono pronto a darti tutte le informazioni che ti servono sulla mia stanza ☺️\nControlla nel pannello dei comandi per vedere cosa posso fare! 🤭")
    else:
        await no_permission_response(update)

async def reading_command(update: Update, context: ContextTypes.DEFAULT_TYPE):
    # Retrieve sensor data from the queue
    reading = reading_queue.get()

    now = datetime.now()
    current_time = now.strftime("%H:%M:%S")
    current_date = now.strftime("%d/%m/%Y")
    
    # Lock access to the shared variable
    with reading_lock:
        if update.message.chat.id == AUTH_USER_ID:
            message_values = [
                "Temperatura interna: {} °C",
                "Umidità interna: {} %",
                "Pressione atmosferica: {} hPa",
                "Temperatura esterna: {} °C",
                "Umidità esterna: {} %"
            ]
            combined_message = "\n".join(message.format(value) for message, value in zip(message_values, reading))  # Use reading directly
            print(f"\nMessaggio inviato a {update.message.chat.id}: \n{combined_message}\n")
            await update.message.reply_text(f"*Valori attuali 🌡️:*\nOra: {current_time}\nData: {current_date}\n\n{combined_message}", parse_mode='Markdown')


        else:
            await no_permission_response(update)

## Sends a message when the temperature reaches the specified value
async def temperature_alert_command(update: Update, context: ContextTypes.DEFAULT_TYPE):
    if update.message.chat.id == AUTH_USER_ID:
        await update.message.reply_text("Inserisci la temperatura a cui vuoi ricevere un allarme 🔔")
        return WAITING_FOR_TEMPERATURE
    else:
        await no_permission_response(update)
        return ConversationHandler.END

async def set_temperature_alert(update: Update, context: ContextTypes.DEFAULT_TYPE):
    if update.message.chat.id == AUTH_USER_ID:
        try:
            desired_temperature = float(update.message.text)
            context.user_data['desired_temperature'] = desired_temperature
            await update.message.reply_text(f"Allarme impostato per {desired_temperature}°C. \nTi avviserò quando la temperatura interna raggiungerà questo valore.")
            return ConversationHandler.END
        except ValueError:
            await update.message.reply_text("Per favore, inserisci un numero valido.")
            return WAITING_FOR_TEMPERATURE
    else:
        await no_permission_response(update)
        return ConversationHandler.END

async def check_temperature_with_alert(app: Application, context: ContextTypes.DEFAULT_TYPE):
    while True:
        with reading_lock:
            current_reading = reading_queue.queue[0] if not reading_queue.empty() else None

        if current_reading is not None:
            internal_temperature = current_reading[0]
            for user_id, user_data in app.user_data.items():
                if 'desired_temperature' in user_data:
                    desired_temperature = user_data['desired_temperature']
                    print(f"\nUser {user_id} has set an alert for {desired_temperature}°C\n")  # Debugging line
                    if internal_temperature >= desired_temperature:
                        await app.bot.send_message(chat_id=user_id, text=f"*Attenzione:* la temperatura interna ha raggiunto {internal_temperature}°C! 🔥", parse_mode='Markdown')
                        del user_data['desired_temperature']
                        print(f"\nAlert sent to user {user_id}\n")  # Debugging line

        await asyncio.sleep(30)


# Command to delete all temperatures from the queue
async def delete_all_temperatures_command(update: Update, context: ContextTypes.DEFAULT_TYPE):
    if update.message.chat.id == AUTH_USER_ID:
        for user_id, user_data in context.application.user_data.items():
            if 'desired_temperature' in user_data:
                del user_data['desired_temperature']
        await update.message.reply_text("Tutte le temperature impostate sono state rimosse.")
    else:
        await no_permission_response(update)


# Responses
def handle_response(text: str) -> str:
    processed: str = text.lower()

    if "misura" in processed:
        return "Se vuoi sapere le misurazioni, usa il comando /reading"

    return "Non ho capito cosa vuoi dire oppure il comando non esiste 😕"

# Response for unauthorized users
async def no_permission_response(update: Update):
    await update.message.reply_text("Non hai il permesso di usare questo bot 🚫")
    print(f"L'utente ({update.message.chat.id}) non ha i permessi necessari per usare il bot")

# Messages
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
        # Better to separate the ifs so if I am in a group chat and authenticated,
        # but haven't explicitly asked the bot to write to me, it writes nothing
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

    # Commands
    app.add_handler(CommandHandler("start", start_command))
    app.add_handler(CommandHandler("reading", reading_command))
    app.add_handler(CommandHandler("delete_temperature", delete_all_temperatures_command))

    # Conversation handler for temperature alert
    conv_handler = ConversationHandler(
        entry_points=[CommandHandler("temperature_alert", temperature_alert_command)],
        states={
            WAITING_FOR_TEMPERATURE: [MessageHandler(filters.TEXT & ~filters.COMMAND, set_temperature_alert)],
        },
        fallbacks=[],
    )
    app.add_handler(conv_handler)

    # Messages
    app.add_handler(MessageHandler(filters.TEXT, handle_message))

    # Errors
    app.add_error_handler(error)

    # Start the task to check temperature
    loop = asyncio.get_event_loop()
    loop.create_task(check_temperature(app))
    loop.create_task(check_temperature_with_alert(app, context=ContextTypes.DEFAULT_TYPE))


    # Bot polling
    print("Polling...")
    app.run_polling(poll_interval=3)