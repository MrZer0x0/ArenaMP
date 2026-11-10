// ArenaMP U025 — дополнения к components/misc/arenarussiantranslator.hpp
//
// Лаунчер локализован не через .ts/.qm, а таблицей «английский исходник ->
// русский перевод» в RussianTranslator::translations(). Поэтому все строки
// в коде лаунчера пишутся ПО-АНГЛИЙСКИ внутри tr(), а русский добавляется
// сюда. Строка без записи в этой таблице останется английской даже в
// русской системе — именно так и выглядит забытый перевод.
//
// Вставить эти строки якорным патчем внутрь лямбды translations(),
// перед "return m;". Файл собирается как UTF-8 (/utf-8 для MSVC уже
// прописан в apps/launcher/CMakeLists.txt), поэтому литералы u8"..."
// безопасны.

                m.insert(QString::fromUtf8(u8"ArenaMP chat sign-in"), QString::fromUtf8(u8"Вход в чат ArenaMP"));
                m.insert(QString::fromUtf8(u8"The character name and password of the game server, the same as in game.\nA character is created in game only: press Play and join the server."), QString::fromUtf8(u8"Имя персонажа и пароль игрового сервера — те же, что при входе в игру.\nПерсонаж создаётся только в игре: нажмите «Играть» и войдите на сервер."));
                m.insert(QString::fromUtf8(u8"Character name"), QString::fromUtf8(u8"Имя персонажа"));
                m.insert(QString::fromUtf8(u8"Password"), QString::fromUtf8(u8"Пароль"));
                m.insert(QString::fromUtf8(u8"In-game code"), QString::fromUtf8(u8"Код из игры"));
                m.insert(QString::fromUtf8(u8"Sign in"), QString::fromUtf8(u8"Войти"));
                m.insert(QString::fromUtf8(u8"Sign in with an in-game code (/chatlink)"), QString::fromUtf8(u8"Войти по коду из игры (/chatlink)"));
                m.insert(QString::fromUtf8(u8"Sign in with a password"), QString::fromUtf8(u8"Войти по паролю"));
                m.insert(QString::fromUtf8(u8"Sign out"), QString::fromUtf8(u8"Выйти"));
                m.insert(QString::fromUtf8(u8"Send"), QString::fromUtf8(u8"Отправить"));
                m.insert(QString::fromUtf8(u8"Message…"), QString::fromUtf8(u8"Сообщение…"));
                m.insert(QString::fromUtf8(u8"%1 · level %2"), QString::fromUtf8(u8"%1 · уровень %2"));
                m.insert(QString::fromUtf8(u8"In game"), QString::fromUtf8(u8"В игре"));
                m.insert(QString::fromUtf8(u8"Connecting to the game server…"), QString::fromUtf8(u8"Соединяемся с игровым сервером…"));
                m.insert(QString::fromUtf8(u8"Enter the character name and password"), QString::fromUtf8(u8"Введите имя персонажа и пароль"));
                m.insert(QString::fromUtf8(u8"Wrong name or password"), QString::fromUtf8(u8"Неверное имя или пароль"));
                m.insert(QString::fromUtf8(u8"Character not found. Join the server and create it in game."), QString::fromUtf8(u8"Персонаж не найден. Зайдите на сервер и создайте его в игре."));
                m.insert(QString::fromUtf8(u8"This account is blocked"), QString::fromUtf8(u8"Учётная запись заблокирована"));
                m.insert(QString::fromUtf8(u8"Too many attempts, please wait"), QString::fromUtf8(u8"Слишком много попыток, подождите"));
                m.insert(QString::fromUtf8(u8"The code expired, get a new one with /chatlink"), QString::fromUtf8(u8"Код устарел, получите новый командой /chatlink"));
                m.insert(QString::fromUtf8(u8"Launcher version does not match the server"), QString::fromUtf8(u8"Версия лаунчера не совпадает с сервером"));
                m.insert(QString::fromUtf8(u8"Connection to the server was lost"), QString::fromUtf8(u8"Соединение с сервером потеряно"));
                m.insert(QString::fromUtf8(u8"The server is not responding"), QString::fromUtf8(u8"Сервер не отвечает"));
                m.insert(QString::fromUtf8(u8"The server sent malformed data"), QString::fromUtf8(u8"Сервер прислал некорректные данные"));
                m.insert(QString::fromUtf8(u8"Voice chat"), QString::fromUtf8(u8"Голосовой чат"));
                m.insert(QString::fromUtf8(u8"Disabled"), QString::fromUtf8(u8"Выключен"));
                m.insert(QString::fromUtf8(u8"Lobby"), QString::fromUtf8(u8"Лобби"));
                m.insert(QString::fromUtf8(u8"In game: nearby players are audible"), QString::fromUtf8(u8"В игре: слышно игроков рядом"));
                m.insert(QString::fromUtf8(u8"Voice is disabled on this server"), QString::fromUtf8(u8"Голос на сервере выключен"));
                m.insert(QString::fromUtf8(u8"Mode"), QString::fromUtf8(u8"Режим"));
                m.insert(QString::fromUtf8(u8"Push to talk"), QString::fromUtf8(u8"Кнопка рации"));
                m.insert(QString::fromUtf8(u8"Voice activity"), QString::fromUtf8(u8"По голосу"));
                m.insert(QString::fromUtf8(u8"Key"), QString::fromUtf8(u8"Клавиша"));
                m.insert(QString::fromUtf8(u8"Microphone"), QString::fromUtf8(u8"Микрофон"));
                m.insert(QString::fromUtf8(u8"Output"), QString::fromUtf8(u8"Вывод"));
                m.insert(QString::fromUtf8(u8"Default"), QString::fromUtf8(u8"По умолчанию"));
                m.insert(QString::fromUtf8(u8"Sensitivity"), QString::fromUtf8(u8"Чувствительность"));
                m.insert(QString::fromUtf8(u8"Volume"), QString::fromUtf8(u8"Громкость"));
                m.insert(QString::fromUtf8(u8"Mute microphone"), QString::fromUtf8(u8"Заглушить микрофон"));
                m.insert(QString::fromUtf8(u8"Unmute microphone"), QString::fromUtf8(u8"Включить микрофон"));
                m.insert(QString::fromUtf8(u8"Speaking: %1"), QString::fromUtf8(u8"Говорят: %1"));
                m.insert(QString::fromUtf8(u8"Could not open the local voice bridge"), QString::fromUtf8(u8"Не удалось открыть локальный мост голоса"));
                m.insert(QString::fromUtf8(u8"Voice: the ticket expired, reconnecting"), QString::fromUtf8(u8"Голос: тикет устарел, переподключаемся"));
                m.insert(QString::fromUtf8(u8"Voice: the server refused the connection (code %1)"), QString::fromUtf8(u8"Голос: сервер отклонил подключение (код %1)"));
                m.insert(QString::fromUtf8(u8"Chat"), QString::fromUtf8(u8"Чат"));
