# Model bezpieczeństwa i ograniczenia

## Zakres naprawy

Narzędzie przebudowuje wyłącznie trzy nazwane atrybuty indeksu `$R` rekordu
MFT 26 (`$Extend/$Reparse`):

1. usuwa `$INDEX_ALLOCATION:$R`;
2. usuwa `$BITMAP:$R`;
3. resetuje `$INDEX_ROOT:$R` do pustego indeksu;
4. dodaje ponownie dokładnie ten sam, wcześniej zweryfikowany zestaw kluczy.

Nie zmienia atrybutów `$REPARSE_POINT` plików. Po operacji Windows nadal musi
uruchomić `chkdsk /f /x`, aby zatwierdzić wolumin i wyczyścić stan naprawy.

## Warunek wejściowy

Ta metoda jest dopuszczalna tylko wtedy, gdy audyt wykaże jednocześnie:

- wszystkie klucze `$R` wskazują aktywne rekordy MFT z zgodną sekwencją;
- każdy rekord ma flagę reparse i atrybut `$REPARSE_POINT` z tym samym tagiem;
- każdy aktywny, obsługiwany atrybut reparse z MFT znajduje się w `$R`;
- nie ma brakujących, stale ani zduplikowanych kluczy;
- liczba wpisów indeksu jest równa liczbie obsługiwanych rekordów MFT;
- uszkodzenie jest strukturalne, np. `ntfsinfo` nie potrafi odczytać
  `$INDEX_ALLOCATION`.

Jeżeli semantyka nie jest spójna, należy użyć narzędzia odzyskiwania danych lub
specjalistycznej naprawy, a nie tego projektu.

## Zabezpieczenia

- audytor zawsze montuje `NTFS_MNT_RDONLY | NTFS_MNT_FORENSIC`;
- zapis wymaga urządzenia blokowego wskazanego przez `/dev/disk/by-id/...-partN`;
- wrapper sprawdza model, serial, rozmiary, PARTUUID i brak montowania;
- konfiguracja jest parsowana jako dane z listą dozwolonych kluczy, bez `eval`
  i bez wykonywania `recovery.env`;
- tożsamość celu i brak montowania są sprawdzane ponownie bezpośrednio przed
  uruchomieniem programu zapisującego;
- suma kopii metadanych jest weryfikowana przed odblokowaniem zapisu;
- liczba kluczy musi zgadzać się z zatwierdzoną wartością z audytu;
- potrzebny jest dosłowny token `DATA_LOSS_IS_POSSIBLE`;
- `trap` przywraca read-only także po błędzie;
- klucze przed i po są porównywane bajtowo;
- wynik jest kontrolowany przez niezależny audyt i `ntfsinfo`.

## Istotny detal formatu NTFS

Dla indeksu widoku `$Extend/$Reparse:$R` pole `INDEX_ROOT.type` musi mieć
wartość zero (`AT_UNUSED`). Ustawienie `AT_REPARSE_POINT` (`0xc0`) tworzy
indeks czytelny dla libntfs, lecz Windows CHKDSK przebudowuje jego korzeń.

## Ograniczenia

- skaner MFT obsługuje reparse tag przechowywany rezydentnie; przypadek
  nierezydentny zatrzymuje procedurę;
- kod zależy od wewnętrznego API `libntfs` z `ntfsprogs-plus`;
- test na jednej wersji biblioteki nie gwarantuje działania na innych;
- kopia metadanych `ntfsclone --metadata` nie zawiera pełnej treści plików;
- awaria zasilania podczas krótkiego okna zapisu może uszkodzić wolumin;
- BitLocker, Storage Spaces, RAID, dyski dynamiczne i wielopartycyjne układy
  wymagają osobnej analizy.
