Вариант 3: Пул на `std::packaged_task`, `std::future`, `std::promise`.

Роли:
Пасечников Михаил: сетевой слой (POSIX sockets), HTTP-парсер, пул потоков на packaged_task.
Кирилл Турышев: хранилище в памяти, синхронизация, логика 8 ручек API, JSON.

Сборка:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
