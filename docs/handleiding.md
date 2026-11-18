# OpenPelletBridge – handleiding

OpenPelletBridge is een klein kastje (een ESP32) dat op de seriële aansluiting van de pelletkachel zit. Je kan er de kachel mee opvolgen en bedienen vanuit een browser, of via Home Assistant. De afstandsbediening en de knoppen op de kachel blijven gewoon werken.

Deze handleiding is voor wie de kachel gebruikt. De technische uitleg (API, registers, ontwikkeling) staat in de [README](../README.md).

## De app openen

- Surf naar **http://openpelletbridge.local**, of naar het IP-adres van het kastje.
- **Op je gsm als app:**
  - iPhone: open de pagina in Safari, tik op *Deel* en kies *Zet op beginscherm*.
  - Android: kies in Chrome *App installeren* of *Toevoegen aan startscherm*.
- De app werkt alleen in je eigen netwerk. Van buitenaf heb je bijvoorbeeld een VPN of Home Assistant nodig.

## Wat je ziet

- **Bovenaan:**
  - de toestand van de kachel in woorden, bijvoorbeeld "Brandt" of "Stand-by door energy saving";
  - de kamertemperatuur en de gewenste temperatuur;
  - het volgende timermoment.
- **Twee bolletjes in de kop:**
  - links de verbinding met de kachel: rood = de kachel antwoordt niet, controleer de kabel;
  - rechts de verbinding met het kastje zelf.
- **Meldingen (gekleurde balken):**
  - een alarm of een actieve fout van de kachel;
  - weinig pellets;
  - geen antwoord van de kachel;
  - **E1 met aftelling.** Staat het deksel van de pelletbak open, dan toont de app "E1: de kachel grijpt in over 42 s". Je hebt ongeveer 55 seconden om het deksel te sluiten. Daarna schakelt de kachel uit.
- **Bediening:** aan/uit, gewenste temperatuur, vlamvermogen (1–4), kamerventilator, energy saving, verbrandings- en pelletkwaliteit, en de pieptoon.
- **Geschiedenis:** een grafiek van de laatste 24 uur of 7 dagen, met temperaturen, vlamvermogen, ventilator en de momenten van elke toestandswissel. Tijdens een periode zonder verbinding met de kachel blijft de grafiek leeg.
- **Toestandswissels, fouten en pelletreserve:** een lijst met datum en uur van elke wissel en elke fout. De kachel bewaart zelf niet wanneer een fout gebeurde; deze lijst wel.
- **Foutgeheugen:** de laatste 5 fouten van de kachel. Je kan er één of alle wissen.
- **Klok** en **Timers (chrono):** de klok van de kachel gelijkzetten, en de 6 tijdprogramma's bekijken of aanpassen.

## Pellets

De rij **Pellets in de bak** schat hoeveel pellets er nog zijn tot het pellet-laag-alarm, bijvoorbeeld "≈ 6,2 kg tot het alarm · 4 u 09 bij het huidige vermogen · alarm verwacht wo 18/11 21:15 · bijgevuld 18/11/2026 08:30 (15 kg)".

- **Het meten leert vanzelf.** Na één keer bijvullen bij het alarm en het volgende alarm is er een eerste schatting. Elke volgende tankbeurt maakt ze nauwkeuriger. Tot dan staat er "wordt gemeten".
- **Bijvullen bij het alarm** hoef je niet in te geven. Gaat het alarm weer uit, dan rekent de app met de standaardhoeveelheid (15 kg).
- **Bijgevuld zonder alarm, of een andere hoeveelheid?** Tik op **Bijgevuld…** en geef het aantal kg in. Anders klopt de telling niet meer.
- De standaardhoeveelheid pas je aan in Expert-modus, bij *Pelletverbruik (gemeten)* → *Standaard…*.
- Na het alarm telt de kachel zelf af: eerst ongeveer 20 minuten tot het reserve-alarm, daarna de minuten tot ze uitschakelt (*PELLET RESERVE* op de afstandsbediening). De app toont die aftelling ook.

## Updates

- Komt er een nieuwe versie uit, dan verschijnt bovenaan een knop **Update**. De installatie gebeurt alleen als jij ze start, in *Toestel*. Het kastje herstart daarna; de kachel blijft gewoon branden.
- **Blijft het kastje na een update onbereikbaar?**
  1. Wacht een paar minuten.
  2. Werkt het dan nog niet, haal de stroom van het **kastje** (niet van de kachel) even af.
  3. In *Toestel* staat daarna wat de laatste stap van de update was. Geef dat door bij een probleemmelding.

## Beveiliging

Iedereen in je netwerk kan de app gebruiken, tenzij je de **API-key verplicht** maakt (in *Toestel*). Dat raden we aan. Je browser onthoudt de key. Andere toestellen, scripts of Home Assistant moeten de key ook meekrijgen.

## Expert en Debug

Met de knoppen **Expert** en **Debug** in de kop zie je meer technische details: ventilatoren, schroef, registers, firmwarelog en de communicatie met de kachel. Voor gewoon gebruik heb je ze niet nodig.

## Problemen

| Wat je ziet | Wat te doen |
|---|---|
| Rood bolletje links, "Geen antwoord van de kachel" | Kabel tussen kastje en kachel nakijken; staat de kachel onder spanning? |
| De app laadt niet | Ping het IP-adres. Antwoordt het niet, haal dan de stroom van het kastje even af. |
| "Gegevens van de kachel zijn … s oud" | Tijdelijk druk op de verbinding; verdwijnt meestal vanzelf. |
| Het kastje herstartte vanzelf | Zet *Expert* aan: in *Toestel* staat dan de reden van de laatste start. Na een crash bewaart het kastje ook technische gegevens voor de ontwikkelaar (`/api/coredump`). |
