# CRTBender

Szoftveres geometria-korrekció CRT monitorokhoz Windowson. A program elő-torzítja
az asztal képét úgy, hogy a monitor saját torzítása után egyenes legyen - fizikai
állítgatás, szervizmenü és forrasztópáka nélkül.

A korrekció a **teljes képernyőre** vonatkozik: egy 15×15-ös rács (állítható,
3×3 … 21×21) fekszik az egész képen, és bármelyik pontját fel-le (igény szerint
balra-jobbra is) mozgatva alakítod a geometriát - sarkok, szélek, közép, bárhol.

---

## Mit tud

- **Rendszerszintű**: az egész elsődleges monitor képét korrigálja, nem
  alkalmazásonként.
- **15×15-ös rács a teljes képen** (225 kontrollpont), monoton köbös
  interpolációval (lásd lentebb, miért nem Catmull-Rom). Igény szerint 3×3-tól
  21×21-ig állítható: durva formára kevesebb pont kényelmesebb, helyi
  szabálytalanságra sűrűbb kell.
- **Profilok felbontás + frissítési frekvencia szerint.** CRT-n a geometria
  timingfüggő: az `1600x1200@85` és az `1280x960@75` másképp torzul. A program
  felismeri a módváltást és automatikusan vált profilt.
- **Tesztminta** (rácsháló + keret + középkereszt) az asztal fölé vagy fekete
  háttérre, hogy legyen mihez igazítani.
- **Gyorsbillentyűk** kalibráláshoz:
  - `Ctrl+Alt+B` - korrekció ki/be (A/B összehasonlítás, egyben **pánik gomb**)
  - `Ctrl+Alt+G` - tesztminta léptetése
  - `Esc` - tesztminta ki (csak amíg a minta látszik, akkor sem veszi el más
    programtól)
  - `Ctrl+Alt+E` - kalibráló ablak
- **Nyelv**: angol és magyar, a tálcamenüből vagy a kalibráló ablakból váltható.
  Alapértelmezés angol; a választás a CFG fájlba kerül, és induláskor betöltődik.
- **Tálcaikon**, CFG fájlba mentett beállítások, opcionális indulás a Windowszal.

## Mit nem tud (őszintén)

- **Exkluzív teljes képernyős játékok** fölé nem tud rajzolni. Borderless
  windowed módban működik. (Ezekhez a warp-shadert a játék render-pipeline-jába
  kellene beépíteni - lásd a Tervek szakaszt.)
- **DRM-védett videó** (Netflix, Disney+ böngészőben) feketén jelenik meg a
  rögzítésben, mert a Desktop Duplication így adja vissza. Ilyenkor kapcsold ki
  a korrekciót `Ctrl+Alt+B`-vel.
- **Az egérkurzort nem torzítja.** A kurzort a hardver rajzolja mindenek fölé;
  a korrekció mértékével (néhány pixel) eltér a tartalomtól a képernyő tetején.
  Ez tudatos döntés: egy szoftveresen rajzolt, torzított kurzor elmosódna.
- **Egy monitor.** Jelenleg mindig az elsődleges monitort veszi alapul.
- **Újramintavételezés**, tehát a hajlított részek minimálisan lágyulnak. Ez
  matematikai szükségszerűség: ha a képet töredékpixelnyivel eltolod, újra kell
  mintavételezni. A veszteség viszont csökkenthető, lásd a „Képminőség" szakaszt.
- Egy képkockányi késleltetést ad hozzá. Asztali munkára észrevehetetlen.

---

## Kész program

A lefordított `dist/CRTBender.exe` benne van a repóban - letöltöd, elindítod,
kész. Egyetlen fájl, nincs telepítés és nincs futásidejű függősége (statikus
CRT), a beállításokat a `%APPDATA%\CRTBender` alá írja.

> **Figyelem:** ez a bináris mingw-w64 keresztfordítóval, Linuxon készült, és
> **nem futott még Windowson**. A kód fordul és a matematikai tesztek átmennek,
> de a tényleges futást (D3D11, Desktop Duplication, tálcaikon) csak éles gépen
> lehet ellenőrizni. Ha valami nem indul, a `%APPDATA%\CRTBender\crtbender.log`
> minden hibakódot naplóz. Éles használatra a saját, Visual Studióval fordított
> build a biztosabb - lásd alább.

Újrafordítás Linuxon: `./tools/build.sh` (ez frissíti a `dist/CRTBender.exe`-t
és lefuttatja a teszteket).

## Fordítás Windowson

Kell hozzá: **Visual Studio 2019/2022** (C++ desktop workload) és **CMake 3.20+**.
Nincs külső függőség.

```powershell
cmake -B build -A x64
cmake --build build --config Release
# eredmény: build\Release\CRTBender.exe
```

A `d3dcompiler_47.dll` a Windows része, nem kell mellécsomagolni. Az exe statikus
CRT-vel épül, tehát nem igényel Visual C++ Redistributable-t.

A warp-matematika platformfüggetlen, külön is tesztelhető:

```powershell
cmake --build build --target warp_tests
.\build\Debug\warp_tests.exe
```

---

## Használat

Első indításkor magától megnyílik a kalibráló ablak. Utána a program a tálcán
lakik; bal klikk a kalibrálásra, jobb klikk a menüre.

### Kalibrálási menet

1. **Kapcsold be a tesztmintát** (`Ctrl+Alt+G` vagy a legördülő a panelen).
   Kezdd az „Asztal felett" opcióval, hogy közben lásd a szerkesztőt is.
2. **Menj végig a képen.** A rács az egész képernyőt lefedi, tehát nem csak a
   felső élt tudod állítani: a szélek íve (pincushion/barrel), a sarkok
   behúzása, a trapéz, sőt a kép közepének helyi szabálytalansága is
   ugyanígy megy. Ahol egyenesnek kellene lennie egy vonalnak és nem az, ott
   húzd a hozzá legközelebbi rácspontokat.
3. **A korrekció iránya fordított a hibáéval.** Ha a monitor *felfelé* hajlítja
   ott a képet, húzd a pontot *lefelé*. A „Bal/jobb tükrözés" bekapcsolva a
   másik oldal automatikusan követi - a CRT torzítása szinte mindig szimmetrikus
   a függőleges tengelyre.
4. **Zárold, ami már jó.** A rács bal szélén minden sornál van egy kis lakat.
   Ha egy sáv rendben van, zárold, és onnantól véletlenül sem mozdul el.
5. **Finomhangolj a nyilakkal.** Nyíl = 0,25 px, `Shift`+nyíl = 1 px,
   `Ctrl`+nyíl = 0,05 px. A panelen mindig látod a kijelölt pont eltolását
   képernyőpixelben.
6. **Elrontottál egy pontot?** Kattints rá duplán, és visszaugrik nullára.
   `Ctrl+Z` visszavonja az utolsó lépést.
7. **Ellenőrizd `Ctrl+Alt+B`-vel.** Ki-be kapcsolgatva azonnal látszik, javult-e.
8. Ha kész, `Mentés` - vagy csak zárd be az ablakot, az is ment.

Amit nem mozgatsz, az **pontosan a helyén marad** - a monoton interpoláció miatt
a korrekció nem „szivárog át" a kép jó részeire. Ezért nyugodtan lehet egy-egy
problémás területet külön-külön rendbe tenni.

### Mekkora rács kell?

A 15×15 jó alapértelmezés: egy 1600×1200-as képen ~107 pixelenként van
kontrollpont. Ha csak a kép nagy léptékű íveit igazítod, a 7×7 vagy 9×9
kényelmesebb (kevesebb pont, gyorsabb munka). Ha egy kicsi, helyi
szabálytalanságot kell kilőni, válts 21×21-re. Váltásnál a meglévő alak
megmarad: a program újramintavételezi a görbét az új rácsra.

### A szerkesztő nagyítása

A valódi korrekció pár pixel 1200-ból, ami az előnézeti rácson láthatatlan lenne.
A „Szerkesztő nagyítás" csúszka **csak az előnézetet** nagyítja (alapból 8×), a
tényleges képre nincs hatása. Egyben a húzás érzékenységét is ez szabja meg:
nagyobb nagyítás = finomabb húzás.

### Képminőség

Ahol a kép el van tolva egy töredékpixellel, ott újramintavételezés történik, és
ez elkerülhetetlenül lágyít valamennyit. Három dolog számít:

- **Az újramintavételezés minősége** (a panelen állítható):
  - *Bilineáris* - a leggyorsabb és a leglágyabb.
  - *Bikubikus* - kiegyensúlyozott (a korábbi alapértelmezés).
  - *Éles - Lanczos + gyűrűzésgátlás* - **ez az új alapértelmezés.** A Lanczos-3
    ablakfüggvény megtartja azokat a magas frekvenciákat, amiket a bikubikus
    lekerekít, így a korrigált részek nagyjából olyan élesek maradnak, mint az
    érintetlenek. Az élesebb szűrők viszont túllőnek a kontrasztos éleknél, ami
    szöveg körül glóriaként látszik; ezért a program a végeredményt a négy
    legközelebbi minta értéktartományába szorítja. Ez pontosan a túllövést
    tünteti el, mást nem.
- **Amit nem mozgatsz, az bitre pontos marad.** Nulla eltolásnál mindegyik
  szűrő azonosságra egyszerűsödik, tehát az érintetlen területek egyáltalán nem
  romlanak. Ezért érdemes csak ott húzni a pontokat, ahol tényleg kell.
- **Az overscan viszont az egész képet rontja.** 100 % fölött a teljes kép
  átméreteződik, tehát mindenhol újramintavételezés lesz. Hagyd 100 %-on; a
  fekete szélek ellen az automatikus szélkitöltés a helyes eszköz.

### Overscan és szélkitöltés

Ha lefelé tolod a kép tetejét, felül elvileg fekete csík maradna. Két megoldás:

- **Szélkitöltés** (alapból automatikus): a rács külső gyűrűje a képernyőn kívülre
  lóg, és a szélső pixelsort keni ki. Ez az alapértelmezés, nem veszít tartalmat.
- **Overscan (zoom)**: 100-115%-os nagyítás a kép közepe körül. Biztosabb, de a
  szélekből levág. Csak akkor kell, ha nagyon nagy a korrekció.

---

## Ha valami nem stimmel

**Pánik gomb: `Ctrl+Alt+B`.** Ez kapcsolja ki a korrekciót, és bármikor
visszaadja a normál képet. A tesztmintából az `Esc` léptet ki.

**Fekete képernyő bekapcsoláskor.** Nyisd meg a naplót (tálcamenü → *Napló
megnyitása*) és keresd a `Capture check:` sort. Ez megmondja, hogy a program
egyáltalán lát-e képet:

- `... - good` → a rögzítés jó, tehát a megjelenítéssel van baj. Próbáld ki a
  `present_mode = flip` beállítást a config fájlban (alapból `bitblt`), majd
  indítsd újra a programot.
- `... - BLANK` → a rögzítés ad üres képet. Ilyenkor a program **magától nem
  jeleníti meg az átfedést**, hogy ne takarja le az asztalt, és ezt a tálcán is
  jelzi. A `Present model:` és `Source texture` sorok mutatják, milyen módban
  fut.

**Nem lehet kattintani semmire.** Ez az 1.0-s kiadás hibája volt (hiányzott a
`WS_EX_LAYERED` az átfedő ablakról); javítva. Ha mégis előfordulna, a program
indulásakor ellenőrzi, hogy az ablak tényleg kattintás-átengedő-e, és ha nem,
el sem indítja a korrekciót - a napló ekkor a
`Overlay is not click-through` sort tartalmazza.

---

## Beállítások fájlja

`%APPDATA%\CRTBender\crtbender.cfg` - sima szöveg, kézzel is szerkeszthető.
Ha a `crtbender.cfg` az exe mellé kerül, a program azt használja (hordozható mód).

```ini
[general]
enabled         = 1
autostart       = 0
pattern_mode    = 0      # 0=ki, 1=asztal felett, 2=fekete hatteren
present_mode    = bitblt # bitblt vagy flip
preview_gain    = 8
language        = en     # en vagy hu
quality         = 2      # 0=bilineáris, 1=bikubikus, 2=éles (Lanczos)

[profile:1600x1200@85]
grid     = 15
overscan = 1.0000
bleed    = auto
locked   = 3,4
row.0    = +0.00000,+0.00333 +0.00000,+0.00417 ...
```

Az eltolások normalizáltak: a képernyő szélességének/magasságának törtrésze.
`+0.005` egy 1200 pixel magas képernyőn 6 pixel lefelé.

Napló: `%APPDATA%\CRTBender\crtbender.log` (a tálcamenüből is megnyitható).

## Indulás a Windowszal

A tálcamenüben vagy a kalibráló ablakban kapcsolható. A
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` kulcsba ír - nem kell hozzá
rendszergazda, és nem telepít szolgáltatást. Ilyenkor a program `--silent`
kapcsolóval indul: csak a tálcán jelenik meg, és betölti a mentett korrekciót.

---

## Hogyan működik

```
DXGI Desktop Duplication ─► D3D11 textúra ─► torzított rács kirajzolása
                                          ─► teljes képernyős, kattintás-átengedő overlay
```

Néhány döntés, ami nem magától értetődő:

**Az overlay kizárja magát a rögzítésből.** Enélkül a saját képét kapná vissza a
Desktop Duplicationtől, és végtelen visszacsatolás lenne. A
`SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` pont ezt oldja meg: az
ablak látszik a fizikai képernyőn, de a rögzítés számára láthatatlan. Ha ez a
hívás nem sikerül (Windows 10 2004-nél régebbi rendszer), a program **nem indítja
el** a korrekciót, hanem hibát jelez. Mellékhatás: a *te* képernyőképeid is a
torzítatlan asztalt mutatják - ami helyes viselkedés.

**A rajzolás külön szálon fut.** A tálca és a szerkesztő a fő szálon él. Ha egy
szálon lennének, egy ablak címsoránál fogva húzása modális ciklusba lépne és
megfagyasztaná a képernyőt.

**Előre-leképezés, nem inverz.** Egy sűrűn tesszellált rács *csúcspontjai*
hordozzák az eltolást, a textúrakoordináták a szabályos rácson maradnak. Így az
interpolációt a raszterizáló végzi, és a shader triviális marad. Az inverz
leképezéshez iteratívan kellene invertálni az eltolásmezőt. A felosztás a
rácsmérethez igazodik: legalább 10 osztás jut minden rácscellába, tehát 15×15-nél
140×140, 21×21-nél 200×200.

**A spline-ok előre ki vannak számolva.** Ez mérés miatt lett így: ha az
interpoláció minden mintavételnél újraszámolja az érintőket, egy `Eval` O(n²), és
a GPU-rács újraépítése 21×21-nél 118 ms-ot vitt el - húzás közben ~8 fps. Mivel a
leképezés szeparálható, a soronkénti görbék egyszer épülnek fel, és az egész
újraépítés O(stride² · n²) helyett O(stride · (stride + n)) lett: 118 ms → 0,93 ms.

**Kikapcsolva sem skálázódik a kép.** Ilyenkor a rács identitás, minden csúcs a
saját texelére esik, a bilineáris minta pontos - vagyis az `Ctrl+Alt+B`
összehasonlítás tényleg csak a korrekciót kapcsolgatja, nem egy újraskálázást is.

**Monoton köbös, nem Catmull-Rom.** Ez mérés alapján változott: Catmull-Rommal
egy 6 px-es felső korrekció ~0,45 px ellentétes irányú elhajlást okozott a
képernyő harmadánál - pont a már jó alsó részen. A Fritsch-Carlson-féle monoton
köbös Hermite-interpoláció ugyanúgy pontosan átmegy minden kontrollponton (6 px-et
húzol, 6 px-et mozdul), de sosem lő túl: amit nem mozgattál, az pontosan a helyén
marad. A `tests/test_warp.cpp` ezt méri.

---

## Tervek

- Több monitor egyidejű korrekciója (most az elsődlegest kezeli).
- Telepítő (`installer/crtbender.iss`, Inno Setup - vázként már itt van).
- Csatornánkénti warp = szoftveres konvergencia-korrekció (külön rács R/G/B-re).
- Ugyanennek a profilnak az exportálása ReShade shaderré, hogy exkluzív teljes
  képernyős játékokban is működjön, extra késleltetés nélkül.
- Kamerás automatikus kalibráció: pontrács kifényképezése, OpenCV-vel megoldott
  inverz warp.

## A projektről

- Készítette: **SubCoderHUN**
- Projekt oldala: <https://github.com/SubCoderHUN/CRTBender>

A programon belül a tálcamenü *Projekt oldala (GitHub)* és *A CRTBender
névjegye...* pontja, valamint a kalibráló ablak *GitHub* gombja is ide vezet.

## Licenc

Lásd a [LICENSE](LICENSE) fájlt.
