# ELEGOO H1 HT bèta 0.17

Voor de **SPA001-MB V1.2 met GD32F303VET6**. Deze versie bouwt voort op bèta 0.16.

## Installatie

Volg de [installatie-instructies](../README.md#install). Onder **Settings → About** verschijnt **Beta 0.17**.

## Drogen

- [Home](screenshots/home-idle.png) neemt het gemiddelde van de geldige TH-metingen van de afgelopen seconde. Tijdens actief drogen past Home de temperatuurbanden van fabrieksfirmware v1.2.5 toe, inclusief de uitzondering op precies 11 °C onder het doel. Bij een doel van 55 °C blijft de weergave op 55 °C als TH tussen 53 en 59 °C schommelt. In rust en pauze gebruikt Home het gemiddelde rechtstreeks. Na de eerste geldige waarde verandert de temperatuur met maximaal 1 °C per seconde. Home toont hele graden in de gekozen eenheid en `--` bij een ongeldige meting.
- Tik op **Set time** op Home en kies **Humidity drying** om de doelwaarde in %RH en de maximale duur in te stellen. Nadat de heater zijn ingestelde temperatuur heeft bereikt, moet de vochtigheid vijf aaneengesloten minuten op of onder het doel blijven. Een ongeldige vochtmeting, open deksel of pauze onderbreekt die meting. De maximale duur telt opwarming mee.
- **Set temp** op Home opent een snelle temperatuurkeuze. Tik op **Apply** om de gewenste temperatuur toe te passen en de andere instellingen van het droogprogramma mee te nemen. Via **Set time** open je alle drooginstellingen in een scrollbare lijst.
- Onder **Filament** staan **Built-in presets** en **My profiles** op aparte schermen met twee kaarten naast elkaar en verticaal scrollen. De kaart toont naam, temperatuur en bij vochtgestuurd drogen ook het doelpercentage. Tik op een kaart om die actief te maken en verder te kunnen bladeren. Alleen een vinkje markeert het actieve profiel.
- **Save current profile** onderaan bewaart de actieve temperatuur, duur en het eventuele vochtdoel. Je vult alleen een naam in. Het nieuwe profiel wordt meteen actief. Via **Edit** kun je een van de maximaal acht eigen profielen aanpassen of verwijderen. Bij terugkeer naar Options blijft de scrollpositie behouden.
- De bediening voor 30 minuten extra is verwijderd. Een eerder opgeslagen cyclus behoudt bij hervatten de resterende tijd uit het herstelrecord.
- De instelgrens is standaard 85 °C. **Settings → Advanced** laat je na een waarschuwing de grens verhogen naar 100 °C. De keuze wordt opgeslagen. Bij het terugzetten naar 85 °C begrenst de firmware de actieve doeltemperatuur en blijven de waarden van opgeslagen profielen bewaard.

## Scherm, grafiek en geluid

- **Settings → Screen timeout** biedt Uit, 1, 5, 15 of 30 minuten. Een eerste tik wekt de achtergrondverlichting. Bij een fout blijft het scherm aan zolang de foutmelding of het bijbehorende logboek openstaat.
- **Drying graph** in het kamerscherm en Diagnostics toont kamertemperatuur, heatertemperatuur en luchtvochtigheid van de huidige of vorige sessie. De grafiek bemonstert bij aanvang elke 30 seconden en vergroot de interval bij langere sessies. De meetgeschiedenis staat in werkgeheugen.
- **Settings → Beeper** bewaart het volume en de voorkeuren voor opstarten, starten, stoppen of voltooien en fouten. Bij het opstarten klinken twee korte piepjes. Het foutsignaal speelt twee keer. De piepertest laat drie korte tonen horen die steeds hoger klinken. Als het volume op 0% staat, gebruikt die expliciet gestarte test 50%.

## Diagnose en fouten

- Diagnostics en de detailweergave vanaf de bovenste tegel van Home verversen de temperaturen elke 500 ms en tonen één decimaal. **Full self-test** doorloopt de onderdelen. Onder **Component tests** kun je afzonderlijk testen. Beide kleppen worden geopend en gesloten met positieterugmelding. De fan wordt gecontroleerd via zijn pulsfeedback. De heater moet onder bewaking binnen 15 seconden minstens 3 °C opwarmen. De spooldrive en beeper vragen om bevestiging tijdens de test. De sensortest controleert op geldige temperatuur- en vochtmetingen. De diagnosetests accepteren een open deksel. De heettest vereist fanfeedback, geldige metingen en een starttemperatuur van maximaal 40 °C. Zij schakelt uit bij 50 °C, bij het behalen van de opwarmtest of na maximaal 15 seconden verwarmen.
- **P01** controleert of de heatersensor binnen één uur minstens de ingestelde temperatuur min 5 °C heeft bereikt.
- **P03** meldt een mogelijke onderbreking door de bimetaalthermostaat wanneer een eerder opgewarmde heater duidelijk afkoelt en tijdens ongeveer twee minuten aantoonbare aansturing zonder merkbaar temperatuurherstel blijft. De sessie stopt met verwarmen. Laat de droger afkoelen, desgewenst met **Manual fan**, en probeer later opnieuw met een lagere temperatuur.
- **Settings → Error log** bewaart de laatste 30 fouten met de gemeten heatertemperatuur, indien beschikbaar. Foutmeldingen bieden een directe knop naar het logboek. De interne flash bewaart het logboek en de instellingen bij gewoon uit- en inschakelen.

**Settings → About** vermeldt Lukas Hilverda en toont een QR-code voor het [GitHub-project](https://github.com/lukashilverda/elegoo-h1-ht-custom-firmware).

[GitHub-screenshots van bèta 0.17](screenshots/)

## Bouwen en controleren

Zie [Build from source](../README.md#build-from-source) voor de benodigdheden en buildstappen. De opdrachten hieronder worden uitgevoerd vanuit de map `firmware/`.

- Firmware en microSD-pakket met ARM GCC 14.2.1 via make package.
- Regelgedrag en flashherstel op de host met GCC 15.2.0 via make test.
- LVGL-rendering en touchscreenbediening via make preview.
- Registermodel en firmwarebediening via python3 hardware_test.py build/h1ht-beta-0.17.bin met Unicorn 2.1.4.

Voor deze build zijn `make test`, `make preview` en de ARM-registertests geslaagd. De tests van Home dekken de v1.2.5-bandgrenzen, het gemiddelde, de tijdbegrenzing, pauze, sensoruitval met herstel en °F.

Applicatie 221352 bytes. Verpakte BIN 262692 bytes. Maximale LVGL-heap in de schermtests 8499 bytes.

Het updatepakket bevat uitsluitend `ELEGOO_DryerBox.bin`. De SHA-256-hashes van deze build zijn hieronder vermeld.

- SHA-256 ZIP `e747a3622d091d60608cb4e70aabe676375e92cdff3b3d81d5584c230c6b370d`
- SHA-256 verpakte BIN `c2c0f4f9e3dbf1a9629993a8d752e607de27644bb18ce08832e95da7e6928174`

De bediening en elektrische en thermische respons op een fysiek apparaat blijven praktijkverificatie. Dat geldt ook voor het hoorbare volume, de mechanische kleppen, de thermostaatdetectie en drogen boven 85 °C.
