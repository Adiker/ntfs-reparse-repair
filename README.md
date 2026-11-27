# NTFS `$Reparse:$R` repair

Defensywna procedura diagnostyczna i eksperymentalne narzędzia do przebudowy
uszkodzonego indeksu systemowego NTFS `$Extend/$Reparse:$R` bez formatowania
woluminu.

Projekt powstał na podstawie rzeczywistego przypadku, w którym:

- Windows stale zgłaszał `dirty` i `Full Repair Needed`;
- dziennik System zawierał zdarzenie NTFS 55 dla rekordu MFT `0x1a`;
- wielokrotne `chkdsk /f /x` kończyło się bez błędów, ale problem wracał;
- wszystkie wpisy reparse były semantycznie poprawne, natomiast
  `$R:$INDEX_ALLOCATION` miał uszkodzoną strukturę;
- przebudowa samego indeksu, a następnie `chkdsk /f /x`, przywróciły stan
  `Healthy / OK` i wyczyściły flagę dirty.

> [!CAUTION]
> To nie jest uniwersalny program do naprawy NTFS. Bezpośrednia modyfikacja
> metadanych może zniszczyć wolumin lub spowodować utratę danych. Najpierw
> wykonaj pełny klon sektorowy. Kopia samych metadanych jest tylko minimum
> awaryjnym i nie zastępuje klonu.

## Co zawiera repozytorium

- `src/ntfs-r-audit.c` — wyłącznie odczytowy audyt indeksu i porównanie z MFT;
- `src/ntfs-r-rebuild.c` — eksperymentalna rekonstrukcja `$R`;
- `scripts/build.sh` — budowanie względem skonfigurowanego `ntfsprogs-plus`;
- `scripts/load-config.sh` — ścisły parser konfiguracji, bez wykonywania pliku;
- `scripts/validate-target.sh` — walidacja stabilnej tożsamości urządzenia;
- `scripts/run-readonly-audit.sh` — wymuszenie read-only i audyt;
- `scripts/rebuild-approved.sh` — kontrolowane okno zapisu z blokadami;
- `recovery.env.example` — szablon parametrów konkretnego nośnika;
- `docs/PROCEDURE.pl.md` — pełna procedura krok po kroku;
- `docs/NTFS-PO-NAGLYM-WYLACZENIU.pl.md` — osobna procedura dla woluminu
  oznaczonego jako dirty po nagłym wyłączeniu, z dostępem tylko do odczytu;
- `docs/PRZYKLAD-2026-11-27.pl.md` — przykład diagnostyki czterech woluminów
  i ograniczeń naprawy przy błędach sprzętowych;
- `docs/DESIGN.md` — założenia, ograniczenia i model bezpieczeństwa.

## Wymagania

- Windows z WSL2 i uprawnieniami administratora;
- dystrybucja Linux z GCC, autotools i narzędziami `util-linux`;
- [`ntfsprogs-plus`](https://github.com/ntfsprogs-plus/ntfsprogs-plus),
  skonfigurowane i zbudowane;
- drugi, zdrowy nośnik na kopię zapasową;
- urządzenie NTFS odłączone od Windows i niezamontowane w Linuxie.

Wersja użyta podczas walidacji: commit
`53943daef955cc2a4a405c8e37c62478e4272fec` projektu `ntfsprogs-plus`.

## Szybki start — tylko laboratorium

```bash
git clone https://github.com/ntfsprogs-plus/ntfsprogs-plus.git
cd ntfsprogs-plus
./autogen.sh
./configure
make -j"$(nproc)"

cd /path/to/ntfs-reparse-repair
./scripts/build.sh /path/to/ntfsprogs-plus
```

Najpierw użyj narzędzia wyłącznie na kopii obrazu:

```bash
sudo build/ntfs-r-audit /path/to/unmounted-metadata-copy.img
```

Nie uruchamiaj `rebuild-approved.sh`, dopóki nie przejdziesz całej procedury z
`docs/PROCEDURE.pl.md` i nie zweryfikujesz naprawy na VHDX lub innym nośniku
testowym.

## Bezpieczeństwo konfiguracji

`recovery.env` jest traktowany jako dane, a nie skrypt powłoki. Parser akceptuje
wyłącznie udokumentowaną listę kluczy oraz wartości proste lub ujęte w jedną
parę apostrofów. Odrzuca nieznane i powtórzone klucze, składnię powłoki oraz
wartości, których nie potrafi jednoznacznie zinterpretować. Mimo tego używaj
wyłącznie własnego pliku konfiguracyjnego i nigdy nie kopiuj gotowej konfiguracji
od niezaufanej osoby.

## Status

Kod jest eksperymentalny i przeznaczony dla osób rozumiejących wewnętrzną
strukturę NTFS. Audytor nie wykonuje zapisów. Narzędzie przebudowujące ma
wielowarstwowe blokady, ale nie eliminuje ryzyka.

## Licencja

Kod jest udostępniany na warunkach GNU General Public License, wersja 2 lub
dowolna późniejsza (`GPL-2.0-or-later`). Zobacz plik `LICENSE`.
