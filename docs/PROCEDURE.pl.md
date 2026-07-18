# Procedura naprawy `$Extend/$Reparse:$R`

## 0. Zatrzymaj się, jeśli dane nie mają kopii

Najbezpieczniejsza kolejność to pełny klon sektorowy, test naprawy na klonie,
a dopiero potem decyzja o zapisie na oryginale. Gdy pełny klon jest niemożliwy,
wykonaj przynajmniej kopię metadanych i zaakceptuj, że nie zapewnia ona pełnego
rollbacku danych użytkownika.

## 1. Potwierdź diagnozę w Windows

Uruchom PowerShell jako administrator:

```powershell
Get-Disk | Format-Table Number,Model,SerialNumber,Size,HealthStatus,OperationalStatus
Get-Partition -DriveLetter A | Format-List *
Get-Volume -DriveLetter A | Format-List *
fsutil dirty query A:
chkdsk A: /scan
```

Sprawdź zdarzenia NTFS:

```powershell
Get-WinEvent -FilterHashtable @{LogName='System'; Id=55} |
  Select-Object -First 20 TimeCreated,ProviderName,Message
```

Kontynuuj wyłącznie, gdy komunikat jednoznacznie dotyczy indeksu `$R` rekordu
MFT 26 (`0x1a`). Inny rekord oznacza inny problem.

## 2. Zanotuj niezmienne cechy celu

Zapisz poza naprawianym dyskiem:

- model i serial/WWN;
- rozmiar dysku i partycji w bajtach;
- offset partycji;
- PARTUUID/GPT GUID;
- rozmiar woluminu NTFS;
- literę dysku i etykietę.

Nie identyfikuj celu wyłącznie numerem `PhysicalDriveN` ani nazwą `/dev/sdX` —
mogą się zmienić po restarcie.

## 3. Odłącz wolumin i podłącz dysk do WSL

W administracyjnym PowerShell, po ponownej weryfikacji tożsamości:

```powershell
mountvol A: /p
wsl --mount \\.\PHYSICALDRIVE<N> --bare
```

W WSL natychmiast wymuś read-only na całym dysku i partycji:

```bash
sudo blockdev --setro /dev/sdX
sudo blockdev --setro /dev/sdX1
sudo blockdev --getro /dev/sdX
sudo blockdev --getro /dev/sdX1
```

Ustal stabilne aliasy w `/dev/disk/by-id` i wypełnij własny `recovery.env` na
podstawie `recovery.env.example`. Nie kopiuj gotowego pliku od innej osoby.
Konfiguracja jest odczytywana przez ścisły parser i nie może zawierać poleceń,
podstawień powłoki ani nieznanych kluczy.

## 4. Wykonaj kopię metadanych

Miejsce docelowe musi znajdować się na innym, zdrowym nośniku:

```bash
sudo ntfsclone --save-image --metadata \
  --output /backup/ntfs-metadata.img "$PARTITION_BY_ID"
sha256sum /backup/ntfs-metadata.img | tee /backup/ntfs-metadata.img.sha256
```

Zachowaj obraz i sumę. Nie przechodź dalej, jeżeli odczyt lub weryfikacja się
nie powiedzie.

## 5. Uruchom audyt wyłącznie do odczytu

```bash
sudo ./scripts/run-readonly-audit.sh recovery.env
```

Wymagany wynik:

```text
AUDIT_SAFE_TO_REBUILD=YES
SUMMARY_STALE=0
MFT_SUMMARY_INDEX_MISSING=0
MFT_SUMMARY_PARSE_ERRORS=0
```

Wpisz `SUMMARY_TOTAL` jako `EXPECTED_KEYS` w `recovery.env` i uruchom audyt
ponownie. Każda rozbieżność oznacza STOP.

## 6. Zweryfikuj strukturę

```bash
sudo /path/to/ntfsprogs-plus/src/ntfsinfo -f -i 26 "$PARTITION_BY_ID"
```

Typowy sygnał uszkodzenia strukturalnego to błąd odczytu
`$INDEX_ALLOCATION`. Sama flaga dirty nie jest wystarczającą przesłanką do tej
naprawy.

## 7. Test na kopii

Odtwórz kopię metadanych do pliku rzadkiego lub VHDX. Uruchom narzędzie w
trybie laboratoryjnym:

```bash
sudo build/ntfs-r-rebuild /path/to/test.img --test-image "$EXPECTED_KEYS"
sudo build/ntfs-r-audit /path/to/test.img
```

Następnie zamontuj VHDX w Windows i wykonaj:

```powershell
chkdsk V: /f /x
chkdsk V: /scan
fsutil dirty query V:
```

Oczekiwane są brak problemów w końcowym skanie, `NOT Dirty` i `Healthy / OK`.

## 8. Świadoma zgoda i zapis na oryginale

Ten etap może spowodować utratę danych. Zamknij wszystkie programy, zapewnij
stabilne zasilanie i jawnie ustaw token tylko dla pojedynczego uruchomienia:

```bash
export NTFS_REPARSE_REBUILD_APPROVAL='DATA_LOSS_IS_POSSIBLE'
sudo --preserve-env=NTFS_REPARSE_REBUILD_APPROVAL \
  ./scripts/rebuild-approved.sh recovery.env
unset NTFS_REPARSE_REBUILD_APPROVAL
```

Wrapper otwiera zapis dopiero po komplecie walidacji i przywraca read-only w
bloku `trap` niezależnie od wyniku.

## 9. Kontrola Windows

Odłącz dysk od WSL:

```powershell
wsl --unmount \\.\PHYSICALDRIVE<N>
```

Przywróć literę partycji po ponownej weryfikacji dysku, a następnie:

```powershell
chkdsk A: /f /x
chkdsk A: /scan
fsutil dirty query A:
Get-Volume -DriveLetter A | Format-List HealthStatus,OperationalStatus
```

Kryteria końcowe:

- oba przebiegi CHKDSK nie zgłaszają problemu `$R`;
- końcowy `/scan` ma kod 0;
- wolumin jest `NOT Dirty`;
- `HealthStatus = Healthy`, `OperationalStatus = OK`;
- po naprawie nie pojawiają się nowe zdarzenia NTFS 55.

## 10. Nie usuwaj od razu kopii

Zachowaj obraz metadanych, logi audytu i konfigurację do czasu wykonania
niezależnej kopii ważnych danych oraz kilku restartów bez nowych błędów.
