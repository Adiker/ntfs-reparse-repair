# NTFS po nagłym wyłączeniu

Ta procedura dotyczy woluminu NTFS oznaczonego jako `dirty`, którego Linux
nie montuje po przerwie w zasilaniu lub resecie. Nie zakłada uszkodzenia
`$Reparse:$R` i nie używa narzędzia do jego przebudowy. Sama flaga `dirty` ani
rozbieżność rekordu 3 `$MFT` i `$MFTMirr` nie dowodzą, że pliki są utracone.

## Najpierw bezpieczeństwo danych

1. Zidentyfikuj wolumin po etykiecie, UUID, rozmiarze i modelu dysku (`lsblk
   -o NAME,FSTYPE,LABEL,UUID,SIZE,MOUNTPOINTS,MODEL`). Nazwy `/dev/sdX` mogą
   się zmieniać. Przed każdym poleceniem sprawdź cel i czy nie jest zamontowany.
2. Sprawdź dziennik jądra i SMART. Status SMART `PASSED` nie wyklucza awarii.
   Błędy odczytu, resetowanie USB lub błędy nośnika wymagają osobnej diagnozy.
   Przy podejrzeniu awarii sprzętu najpierw wykonaj kopię ważnych danych lub
   obraz na zdrowy nośnik, najlepiej narzędziem do odzyskiwania z mapą błędów;
   nie zaczynaj od naprawy i nie wykonuj intensywnych testów powierzchni.
3. Sprawdź, czy jest aktualna, niezależna kopia. Naprawa metadanych może
   zmieniać pliki lub katalogi; najpierw zabezpiecz to, co ważne.

## Tymczasowy dostęp do odczytu w Linuxie

Jeśli sprzęt działa stabilnie, a dostęp jest potrzebny przed uruchomieniem
Windows, można spróbować montowania `ntfs3` w trybie read-only przez osobną,
również read-only warstwę device mapper. Odczyt bez odzyskania dziennika może
pokazać niespójną lub nieaktualną zawartość. Nie korzystaj z tych ścieżek do
uruchamiania programów, zapisu ani jako zamiennika w aplikacjach i usługach.
Przy błędach I/O przerwij odczyt i skup się na odzyskaniu danych.

Przykład dla **jednego odmontowanego** woluminu. Podstaw własny UUID, sprawdź
wynik `lsblk`, użyj unikalnej nazwy mapowania i istniejącego pustego katalogu.
Nie kopiuj przykładów na ślepo:

```bash
lsblk -o NAME,FSTYPE,LABEL,UUID,SIZE,MOUNTPOINTS,MODEL
dev=/dev/disk/by-uuid/TUTAJ_UUID_NTFS
name=ntfs-recovery-ro
target=/mnt/ntfs-recovery
sudo mkdir -p "$target"
sudo blockdev --getsz "$dev"
sectors=$(sudo blockdev --getsz "$dev")
printf '0 %s linear %s 0\n' "$sectors" "$dev" | sudo dmsetup create "$name" --readonly
sudo dmsetup info -c "$name"
sudo mount -t ntfs3 -o ro,force,nosuid,nodev,noexec,uid="$(id -u)",gid="$(id -g)" "/dev/mapper/$name" "$target"
findmnt -T "$target" -o SOURCE,TARGET,FSTYPE,OPTIONS
```

Upewnij się, że `dmsetup` pokazuje mapowanie read-only (`r` w kolumnie Stat),
a `findmnt` pokazuje właściwe źródło i `ro`. Jeśli montowanie się nie powiedzie,
usuń nieużywane mapowanie: `sudo dmsetup remove "$name"`. Po zakończeniu pracy
zamknij korzystające z niego aplikacje, wykonaj `sudo umount "$target"`, a
następnie `sudo dmsetup remove "$name"`. Montowanie i mapowanie są tymczasowe.
Nie używaj `force` na urządzeniu z możliwością zapisu jako sposobu naprawy.

## Sprawdzenie i naprawa w Windows

Po zabezpieczeniu danych zamknij dostęp w Linuxie i uruchom pełny Windows, bez
hibernacji i szybkiego uruchamiania. Dla każdego zdrowego dysku osobno ustal
literę po etykiecie, rozmiarze i identyfikatorze w Zarządzaniu dyskami. W
terminalu administratora zastąp `X:` **zweryfikowaną** literą:

```cmd
fsutil dirty query X:
chkdsk X: /f
chkdsk X: /scan
fsutil dirty query X:
```

`/f` zapisuje poprawki i może wymagać odmontowania woluminu albo restartu.
Nie stosuj automatycznie `/r` ani `ntfsfix -b`: pierwszy może długo i intensywnie
czytać dysk, drugi usuwa listę uszkodzonych sektorów. Zapisz wyniki, sprawdź
ważne pliki i ich kopie, a po pełnym zamknięciu Windows sprawdź montowanie w
Linuxie bez opcji `force`. Jeśli kontrola zgłasza błędy sprzętowe lub ponownie
wraca `dirty`, przerwij powtarzanie napraw i zbadaj nośnik oraz połączenie.

`ntfsfix` nie zastępuje `chkdsk`: potrafi zmienić metadane i wyzerować dziennik
NTFS. `ntfsfix -n` jest próbą bez zapisu, lecz wynik nie stanowi pełnej
kontroli systemu plików. Wyczyszczenie samej flagi `dirty` nie jest naprawą.
