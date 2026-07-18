# Ostrzeżenie

Oprogramowanie jest dostarczane bez gwarancji. Operacje na surowych
metadanych systemu plików mogą skutkować utratą pojedynczych plików, linków,
reparse pointów, całego katalogu albo możliwości zamontowania woluminu.

Przed bezpośrednim zapisem:

1. wykonaj pełny klon sektorowy na inny fizyczny nośnik;
2. zweryfikuj odczyt klonu i jego sumę kontrolną;
3. przećwicz procedurę na kopii lub VHDX;
4. zapewnij stabilne zasilanie;
5. odłącz wszystkie niepotrzebne dyski;
6. potwierdź tożsamość celu co najmniej przez model, serial/WWN, rozmiar,
   PARTUUID i stabilną ścieżkę `/dev/disk/by-id`.

Autorzy i współtwórcy nie odpowiadają za utratę danych.
