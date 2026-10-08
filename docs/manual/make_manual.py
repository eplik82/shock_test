#!/usr/bin/env python3
"""Kasutusjuhend (A4 PDF): docs/Kasutusjuhend_shock_test.pdf
Käivita: PYTHONPATH=<reportlab> python docs/manual/make_manual.py [versioon]"""
import os
import sys
from datetime import date

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (Image, KeepTogether, PageBreak, Paragraph, SimpleDocTemplate, Spacer, Table,
                                TableStyle)

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, 'img')
OUT = os.path.join(HERE, '..', 'Kasutusjuhend_shock_test.pdf')
VER = sys.argv[1] if len(sys.argv) > 1 else '0.2.5'
FONTS = os.path.expanduser('~/esp32-browser/fonts/full')

pdfmetrics.registerFont(TTFont('DV', os.path.join(FONTS, 'DejaVuSans.ttf')))
pdfmetrics.registerFont(TTFont('DVB', os.path.join(FONTS, 'DejaVuSans-Bold.ttf')))
pdfmetrics.registerFont(TTFont('DVM', os.path.join(FONTS, 'DejaVuSansMono.ttf')))

ACC = colors.HexColor('#c8102e')   # logo punane
DARK = colors.HexColor('#262a2e')  # logo taust
MUT = colors.HexColor('#5b6470')
LINE = colors.HexColor('#d9dde3')

st = {
    'body': ParagraphStyle('body', fontName='DV', fontSize=9.5, leading=13.5, spaceAfter=5),
    'h1': ParagraphStyle('h1', fontName='DVB', fontSize=16, leading=20, spaceBefore=6, spaceAfter=8, textColor=DARK),
    'h2': ParagraphStyle('h2', fontName='DVB', fontSize=11.5, leading=15, spaceBefore=8, spaceAfter=4, textColor=DARK),
    'cap': ParagraphStyle('cap', fontName='DV', fontSize=8, leading=10.5, textColor=MUT, alignment=TA_CENTER,
                          spaceBefore=3, spaceAfter=9),
    'cell': ParagraphStyle('cell', fontName='DV', fontSize=8.5, leading=11),
    'cellb': ParagraphStyle('cellb', fontName='DVB', fontSize=8.5, leading=11),
    'note': ParagraphStyle('note', fontName='DV', fontSize=9, leading=12.5, textColor=colors.HexColor('#7a4a00')),
    'step': ParagraphStyle('step', fontName='DV', fontSize=9.5, leading=13.5, leftIndent=14, firstLineIndent=-14,
                           spaceAfter=3),
}
fig_no = [0]


def P(t, s='body'):
    return Paragraph(t, st[s])


def H1(t):
    return P(t, 'h1')


def H2(t):
    return P(t, 'h2')


def steps(items):
    return [P(f'<b>{i}.</b> {t}', 'step') for i, t in enumerate(items, 1)]


def bullets(items):
    return [P(f'•&nbsp;&nbsp;{t}', 'step') for t in items]


def fig(name, caption, width_mm=150, border=True):
    fig_no[0] += 1
    path = os.path.join(IMG, name)
    from PIL import Image as PI
    w, h = PI.open(path).size
    W = width_mm * mm
    img = Image(path, width=W, height=W * h / w)
    t = Table([[img]], colWidths=[W + 2])
    t.setStyle(TableStyle([('BOX', (0, 0), (-1, -1), 0.6 if border else 0, LINE), ('LEFTPADDING', (0, 0), (-1, -1), 1),
                           ('RIGHTPADDING', (0, 0), (-1, -1), 1), ('TOPPADDING', (0, 0), (-1, -1), 1),
                           ('BOTTOMPADDING', (0, 0), (-1, -1), 1)]))
    return KeepTogether([t, P(f'Joonis {fig_no[0]}. {caption}', 'cap')])


def figs_side(items, width_mm=72):
    """Kaks telefonipilti kõrvuti: [(fail, allkiri), ...]"""
    cells, caps = [], []
    from PIL import Image as PI
    for name, cap in items:
        fig_no[0] += 1
        path = os.path.join(IMG, name)
        w, h = PI.open(path).size
        W = width_mm * mm
        cells.append(Image(path, width=W, height=W * h / w))
        caps.append(P(f'Joonis {fig_no[0]}. {cap}', 'cap'))
    t = Table([cells, caps], colWidths=[width_mm * mm + 8] * len(items))
    t.setStyle(TableStyle([('VALIGN', (0, 0), (-1, -1), 'TOP'), ('ALIGN', (0, 0), (-1, -1), 'CENTER')]))
    return KeepTogether([t])


def table(rows, widths, head=True):
    data = [[P(c, 'cellb' if (head and r == 0) else 'cell') for c in row] for r, row in enumerate(rows)]
    t = Table(data, colWidths=[w * mm for w in widths], repeatRows=1 if head else 0)
    style = [('GRID', (0, 0), (-1, -1), 0.4, LINE), ('VALIGN', (0, 0), (-1, -1), 'TOP'),
             ('LEFTPADDING', (0, 0), (-1, -1), 4), ('RIGHTPADDING', (0, 0), (-1, -1), 4),
             ('TOPPADDING', (0, 0), (-1, -1), 3), ('BOTTOMPADDING', (0, 0), (-1, -1), 3)]
    if head:
        style.append(('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#eef0f3')))
    t.setStyle(TableStyle(style))
    return t


def note(t):
    tb = Table([[P(t, 'note')]], colWidths=[170 * mm])
    tb.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, -1), colors.HexColor('#fff4e0')),
                            ('BOX', (0, 0), (-1, -1), 0.6, colors.HexColor('#e0a030')),
                            ('LEFTPADDING', (0, 0), (-1, -1), 6), ('RIGHTPADDING', (0, 0), (-1, -1), 6),
                            ('TOPPADDING', (0, 0), (-1, -1), 4), ('BOTTOMPADDING', (0, 0), (-1, -1), 4)]))
    return KeepTogether([tb, Spacer(1, 6)])


def on_page(c, doc):
    if doc.page == 1:
        return
    c.saveState()
    c.setStrokeColor(LINE)
    c.line(20 * mm, 285 * mm, 190 * mm, 285 * mm)
    c.setFont('DV', 7.5)
    c.setFillColor(MUT)
    c.drawString(20 * mm, 287 * mm, 'Löögitesti seade shock_test — kasutusjuhend')
    c.drawRightString(190 * mm, 287 * mm, f'püsivara v{VER}')
    c.drawString(20 * mm, 10 * mm, 'Threod Systems')
    c.drawRightString(190 * mm, 10 * mm, f'lk {doc.page}')
    c.restoreState()


def cover(c, doc):
    c.saveState()
    c.setFillColor(DARK)
    c.rect(0, 0, A4[0], A4[1], fill=1, stroke=0)
    logo = os.path.join(IMG, 'logo.png')
    W = 170 * mm
    c.drawImage(logo, (A4[0] - W) / 2, 175 * mm, width=W, height=W * 480 / 800)
    c.setFillColor(colors.white)
    c.setFont('DVB', 26)
    c.drawCentredString(A4[0] / 2, 145 * mm, 'Löögitesti seade')
    c.setFont('DV', 14)
    c.drawCentredString(A4[0] / 2, 133 * mm, 'Half-Sine Shock Pulse tester')
    c.setFillColor(ACC)
    c.rect(A4[0] / 2 - 25 * mm, 125 * mm, 50 * mm, 1.2 * mm, fill=1, stroke=0)
    c.setFillColor(colors.white)
    c.setFont('DVB', 18)
    c.drawCentredString(A4[0] / 2, 110 * mm, 'KASUTUSJUHEND')
    c.setFont('DV', 10)
    c.setFillColor(colors.HexColor('#b8bec7'))
    c.drawCentredString(A4[0] / 2, 98 * mm, 'MIL-STD-810H Method 516.8  ·  UN38.3 T.4')
    c.drawCentredString(A4[0] / 2, 40 * mm, f'Püsivara v{VER}  ·  {date.today().strftime("%d.%m.%Y")}')
    c.drawCentredString(A4[0] / 2, 33 * mm, 'Waveshare ESP32-S3-Touch-LCD-4.3  ·  ADXL375')
    c.restoreState()


S = []
S.append(PageBreak())

# ---------------------------------------------------------------- Sisukord
S.append(H1('Sisukord'))
toc = ['1. Ülevaade', '2. Ühendamine ja anduri paigaldus', '3. Peavaade', '4. Kella seadistamine',
       '5. Testi tegemine samm-sammult', '6. Hindamiskriteeriumid', '7. Seaded ja eelseadistused',
       '8. WiFi portaal (telefon, arvuti)', '9. Testraport (PDF, CSV)', '10. Püsivara uuendamine', '11. Veaotsing',
       '12. Tehnilised andmed']
S += [P(t) for t in toc]
S.append(Spacer(1, 10))

# ---------------------------------------------------------------- 1
S.append(H1('1. Ülevaade'))
S.append(P('Löögitesti seade mõõdab <b>poolsiinus-löögiimpulssi</b> (half-sine shock pulse) ja hindab, kas see vastab '
           'seadistatud nõuetele. Seade sobib löögistendi või kukkumistesti häälestamiseks ja kontrollimiseks '
           'standardite <b>MIL-STD-810H Method 516.8</b> (Procedure I, poolsiinus) ja <b>UN38.3 T.4</b> '
           '(liitiumakude löögitest) järgi.'))
S += bullets([
    'mõõdab tippkiirenduse, impulsi kestuse (TD), kiiruse muutuse (ΔV) ja kontrollib impulsi kuju tolerantsikoridoris;',
    'iga löögi tulemus <b>LÄBITUD / EBAÕNNESTUS</b> koos põhjusega kuvatakse kohe ekraanil;',
    'löögid kogutakse <b>seeriatesse</b> (6 suunda × N lööki) ja salvestatakse seadme mällu;',
    'testraport <b>PDF</b> ja <b>CSV</b> kujul laaditakse alla telefoni või arvutisse seadme WiFi kaudu;',
    'kasutajaliides on eesti ja inglise keeles.'])
S.append(note('<b>NB!</b> Seade on kontroll- ja häälestusvahend. ADXL375 MEMS-anduri valimisagedus (1600 Hz) on väiksem '
              'kui MIL-STD-810H Annex A nõue ametlikuks kvalifitseerimismõõtmiseks (≥ 10 × f<sub>max</sub>). '
              'Kvalifitseerimiseks kasuta laboratoorset mõõtesüsteemi.'))

# ---------------------------------------------------------------- 2
S.append(H1('2. Ühendamine ja anduri paigaldus'))
S.append(table([
    ['Osa', 'Kirjeldus'],
    ['Toide', 'USB-C kaabel plaadi <b>UART</b>-pessa (5 V). Ära ühenda laadijat teise (natiivse) USB-pessa.'],
    ['Andur', 'Adafruit ADXL375 (±200 g) plaadi I2C pistikusse: <b>Vin</b> → 3,3 V, GND, SDA, SCL. '
              'Toide peab olema anduri <b>Vin</b> viigus (mitte 3Vo).'],
    ['Paigaldus', 'Kinnita andur jäigalt (kruvidega) löögilaua või katseobjekti külge. Lõtv kinnitus tekitab '
                  'järelvõnkumist ja moonutab impulssi.'],
    ['Telg', 'Mõõdetakse kõiki kolme telge. Seadetes saab valida kindla telje või automaatse valiku (suurim kiirendus).'],
], [35, 135]))
S.append(Spacer(1, 6))
S.append(P('Pärast toite ühendamist kuvatakse käivituslogo ja umbes 3 sekundi pärast avaneb peavaade. '
           'Esimesel käivitusel vormindatakse andmemälu (kuni 2 minutit).'))

# ---------------------------------------------------------------- 3
S.append(PageBreak())
S.append(H1('3. Peavaade'))
S.append(fig('01_peavaade_kell_seadistamata.png', 'Peavaade enne testi. Kollane rida: kell on seadistamata, '
             'kuvatakse seadme WiFi nimi ja parool.'))
S.append(table([
    ['Ala', 'Tähendus'],
    ['Ülemine rida', 'Valitud eelseadistus, seadme WiFi võrgu nimi, seadete nupp (hammasratas).'],
    ['Kollane / hall rida', 'Kollane: kell seadistamata, WiFi nimi ja parool. Hall: kuupäev ja kellaaeg (kell sünkroniseeritud).'],
    ['Olekukast', '<b>ALUSTA SEERIAT</b> · <b>OOTAB LÖÖKI</b> (sinine) · <b>PEATATUD</b> · '
                  '<b>LÄBITUD</b> (roheline) · <b>EBAÕNNESTUS</b> (punane).'],
    ['Suur number', 'Mõõdetud tippkiirendus g-des; all min ja max piir ning riba, millel valged jooned näitavad piire.'],
    ['TD, ΔV, Kuju', 'Mõõdetud kestus ja kiiruse muutus koos lubatud vahemikuga; kuju koridori kontroll, telg ja punktide arv.'],
    ['Graafik', 'Sinine – mõõdetud impulss, hall – nominaalne poolsiinus, punane – tolerantsikoridor.'],
    ['Alumine rida', 'Seeria number ja nimi, järgmine suund ja löök (nt +X 2/3), löökide arv, läbitud; „praegu“ – '
                     'hetke kiirendus.'],
    ['Nupud', '<b>Uus seeria</b> · <b>Valmis</b>/<b>Peata</b> (lööki ootamine) · <b>Tühista viimane</b> · <b>Lõpeta</b>'],
], [35, 135]))

# ---------------------------------------------------------------- 4
S.append(H1('4. Kella seadistamine'))
S.append(P('Seadmel pole patareiga kella. Pärast igat sisselülitamist sünkroniseeritakse kell telefoni või arvuti '
           'kellaga, kui avad seadme veebiportaali (vt peatükk 8). Kuni kell on seadistamata, on peavaates kollane rida; '
           'kellaaja puudumisel märgitakse löögid raportis „kell seadistamata“ ja need dateeritakse tagantjärele, kui kell '
           'samal sisselülitusel sünkroniseeritakse.'))
S.append(fig('00_peavaade_kell.png', 'Kell sünkroniseeritud: kollase rea asemel on kuupäev ja kellaaeg.', 120))

# ---------------------------------------------------------------- 5
S.append(H1('5. Testi tegemine samm-sammult'))
S.append(H2('5.1 Seeria alustamine'))
S += steps([
    'Vali seadetes sobiv <b>eelseadistus</b> (vt peatükk 7) ja kontrolli tippkiirendust A ning kestust TD.',
    'Vajuta peavaates <b>Uus seeria</b>.',
    'Sisesta katseobjekti nimi, seerianumber ja operaator (puudutus väljal avab klaviatuuri; ✓ sulgeb). '
    'UN38.3 kontrollide korral sisesta ka mass ja avatud ahela pinge (OCV) enne testi.',
    'Vajuta <b>Alusta</b>. Seade hakkab lööki ootama (olekukast „OOTAB LÖÖKI“).'])
S.append(fig('04_dialoog_taidetud.png', 'Uue seeria dialoog (UN38.3 kontrollidega: mass ja pinge enne).', 125))
S.append(fig('03_klaviatuur.png', 'Ekraaniklaviatuur eesti tähtedega (õ ä ö ü š ž). ABC – suurtähed, 1# – numbrid.', 125))
S.append(H2('5.2 Löögid'))
S += steps([
    'Tee löök (löögistend / kukkumine). Seade käivitub, kui kiirendus ületab käivitusläve (vaikimisi 30 % A-st), '
    'salvestab impulsi koos eel- ja järelosaga ning kuvab tulemuse umbes 1 sekundi jooksul.',
    'Järgmine löök on võimalik pärast ooteaega (vaikimisi 2 s), mis välistab põrgete ja järelvõnkumise topeltmõõtmise.',
    'Seeria järjekord on <b>+X, −X, +Y, −Y, +Z, −Z</b>, igas suunas 3 lööki (seadistatav). Alumisel real on näha, '
    'millises suunas järgmine löök tuleb – pööra katseobjekti vastavalt.',
    'Vale löögi (nt kogemata käivitus) saab eemaldada nupuga <b>Tühista viimane</b>.',
    '<b>Peata</b> lõpetab lööki ootamise ajutiselt; <b>Valmis</b> jätkab.'])
S.append(fig('05_ootab_looki.png', 'Seade ootab lööki.', 125))
S.append(fig('06_tulemus_labitud.png', 'Löök läbitud: tipp, kestus, ΔV ja kuju on piirides.', 125))
S.append(fig('07_tulemus_ebaonnestus.png', 'Löök ebaõnnestus: punasega on märgitud piiridest väljas olevad '
             'väärtused ja põhjus.', 125))
S.append(H2('5.3 Seeria lõpetamine'))
S.append(P('Kui kõik löögid on tehtud, avaneb lõpetamise dialoog automaatselt; varem saab lõpetada nupuga '
           '<b>Lõpeta</b>. UN38.3 kontrollide korral sisesta mass ja pinge pärast testi ning märgi visuaalse kontrolli '
           'tulemused. Seejärel kuvatakse seeria koondtulemus ja raporti allalaadimise juhis.'))
S.append(fig('10_lopeta_seeria.png', 'Seeria lõpetamine UN38.3 operaatori kontrollidega.', 125))
S.append(fig('11_seeria_lopetatud.png', 'Seeria koondtulemus ja raporti allalaadimise juhis.', 125))

# ---------------------------------------------------------------- 6
S.append(H1('6. Hindamiskriteeriumid'))
S.append(P('Kriteeriumid järgivad MIL-STD-810H Method 516.8 joonist 516.8-5 (poolsiinus). A on nominaalne '
           'tippkiirendus, TD nominaalne kestus. Löök on <b>LÄBITUD</b>, kui kõik otsust mõjutavad tingimused on täidetud.'))
S.append(table([
    ['Näitaja', 'Mõõtmine', 'Vaikimisi piir'],
    ['Tipp', 'Suurim kiirendus impulsis (interpoleeritud valimite vahel)', '0,8·A … 1,2·A (±20 %)'],
    ['Kestus TD', 'Aeg 10 % tipu punktide vahel, teisendatud poolsiinuse baaskestuseks', 'TD ±10 %'],
    ['ΔV', 'Kiiruse muutus = kiirenduse integraal üle impulsi; nominaal 2·A·TD/π', '±20 % (seadetes sees/väljas)'],
    ['Kuju', 'Kõik punktid jälgimisaknas 2,4·TD on koridoris ±0,2·A ja ±0,1·TD', 'seadetes sees/väljas'],
    ['Küllastus', 'Andur ±200 g; üle selle mõõta ei saa', 'küllastus = ebaõnnestus'],
], [25, 95, 50]))
S.append(Spacer(1, 6))
S.append(P('<b>UN38.3 seeria</b> loetakse läbituks, kui kõik löögid on läbitud <b>ja</b> operaatori kontrollid on korras: '
           'massikadu ≤ 0,5 % (mass &lt; 1 g), ≤ 0,2 % (1–75 g) või ≤ 0,1 % (&gt; 75 g), pinge pärast testi ≥ 90 % '
           'testieelsest ning puuduvad leke, gaasi eraldumine, lagunemine, purunemine ja tuli.'))

# ---------------------------------------------------------------- 7
S.append(H1('7. Seaded ja eelseadistused'))
S.append(P('Seaded avanevad peavaate hammasrattast. Muudatused jõustuvad nupuga <b>Salvesta</b>; <b>Loobu</b> jätab '
           'muudatused kehtetuks. Keele või valimisageduse muutmisel seade taaskäivitub.'))
S.append(fig('08_seaded.png', 'Seadete vaade (ülemine osa). Oranž rida näitab kehtivaid piire.', 120))
S.append(fig('09_eelseadistus_menuu.png', 'Eelseadistuse valik.', 120))
S.append(table([
    ['Seade', 'Selgitus'],
    ['Keel / Language', 'Eesti või English (ekraan, portaal, raport).'],
    ['Eelseadistus', 'Standardi järgi A, TD, löökide arv ja tolerantsid; muuda „Kasutaja seaded“ korral käsitsi.'],
    ['Aku mass (kg)', 'UN38.3 aku eelseadistuste korral: A arvutatakse massi järgi (valem standardist).'],
    ['Nominaalne tipp A, kestus TD', 'Nõutav löök. A vahemik 5 g … A ülempiir.'],
    ['A ülempiir', '200 g või 166 g. 166 g puhul mahub ülemine tolerants 1,2·A anduri mõõtepiiri (200 g).'],
    ['Lööke suuna kohta', 'Vaikimisi 3 (kokku 18 lööki 6 suunas).'],
    ['Mõõtetelg', 'Automaatne (suurim kiirendus) või X / Y / Z.'],
    ['Tolerantsid', 'Tipp, TD, ΔV ja kuju koridor protsentides (vaikimisi MIL-STD-810H).'],
    ['Kuju / ΔV kontroll', 'Kas see näitaja mõjutab otsust (väljas = ainult informatiivne).'],
    ['UN38.3 kontrollid', 'Seeria alguses ja lõpus küsitakse massi, pinget ja visuaalset kontrolli.'],
    ['Käivituslävi', 'Mitu % A-st peab kiirendus ületama, et löök salvestataks (vaikimisi 30 %).'],
    ['Ooteaeg pärast lööki', 'Aeg sekundites, mille jooksul uut lööki ei käivitata (vaikimisi 2 s).'],
    ['Valimisagedus', '1600 Hz (soovitatav). 3200 Hz ei jõua I2C kaudu ja annab valimite kadu.'],
    ['Kalibreerimistegurid', 'Telgede tundlikkuse parandus (1,0000 = parandust pole).'],
    ['Pääsupunkti parool', 'Seadme WiFi parool (vähemalt 8 märki; tühi = avatud võrk).'],
], [45, 125]))
S.append(Spacer(1, 6))
S.append(H2('Eelseadistused'))
S.append(table([
    ['Eelseadistus', 'A', 'TD', 'Allikas'],
    ['MIL HSC-I', '20 g', '23 ms', 'MIL-STD-810H 516.8 Procedure I, kiirlaevad'],
    ['MIL HSC-II', '5 g', '23 ms', 'MIL-STD-810H 516.8 Procedure I'],
    ['MIL maapealne', '31,4 g', '11 ms', 'Poolsiinus asendus (π/4 × 40 g)'],
    ['UN38.3 element', '150 g', '6 ms', 'UN38.3 T.4'],
    ['UN38.3 suur element', '50 g', '11 ms', 'UN38.3 T.4 (alternatiiv)'],
    ['UN38.3 väike aku', 'min(150, √(100850/m)) g', '6 ms', 'UN38.3 T.4, m = mass kg'],
    ['UN38.3 suur aku', 'min(50, √(30000/m)) g', '11 ms', 'UN38.3 T.4, m = mass kg'],
], [40, 40, 20, 70]))

# ---------------------------------------------------------------- 8
S.append(PageBreak())
S.append(H1('8. WiFi portaal (telefon, arvuti)'))
S.append(P('Seade loob oma WiFi võrgu <b>ShockTest-XXXX</b> (nimi ja parool on peavaate kollasel real). Portaal on '
           'aadressil <b>http://4.3.2.1</b>. Seade ei vaja internetti.'))
S += steps([
    'Ühenda telefon WiFi-võrguga ShockTest-XXXX.',
    'Telefon avab ise akna „Logi võrku sisse“ – portaal avaneb ja seadme kell sünkroniseeritakse.',
    'Failide allalaadimiseks ja püsivara uuendamiseks vajuta üleval <b>Ava brauseris</b> '
    '(sisselogimise aken ei võimalda faile salvestada ega valida).',
    'Kui aken ei avane, ava brauseris käsitsi aadress http://4.3.2.1.'])
S.append(figs_side([('b4_captive.png', 'Telefoni sisselogimise aken: nupp „Ava brauseris“.'),
                    ('b1_testid.png', 'Vaheleht „Testid“: seeriad, koondtulemus, PDF, CSV, kustutamine.')]))
S.append(P('<b>Testid</b> – kõik seeriad (uuemad eespool) koos koondtulemusega (LÄBITUD / EBAÕNNESTUS / POOLELI). '
           '<b>PDF</b> ja <b>CSV</b> laadivad raporti alla; <b>Kustuta</b> eemaldab seeria seadme mälust. '
           '<b>Seade</b> – püsivara versioon, kell, anduri olek, valimisagedus, mälu kasutus. '
           '<b>Püsivara</b> – uuendamine (peatükk 10).'))
S.append(figs_side([('b2_seade.png', 'Vaheleht „Seade“.'), ('b3_pusivara.png', 'Vaheleht „Püsivara“.')]))
S.append(P('Portaali pildid on tehtud näidisandmetega.', 'cap'))

# ---------------------------------------------------------------- 9
S.append(PageBreak())
S.append(H1('9. Testraport (PDF, CSV)'))
S.append(P('<b>PDF-raport</b> sisaldab: seeria andmed (katseobjekt, seerianumber, operaator, algus), standardi ja seaded, '
           'koondtulemuse, UN38.3 kontrollid, löökide tabeli ning iga löögi graafiku koos tolerantsikoridoriga. '
           'Raport koostatakse seadmes (ca 90 KB, allalaadimine ~6 s).'))
S.append(P('<b>CSV-fail</b> on Exceli jaoks (eesti keeles „;“ eraldaja ja kümnendkoma, inglise keeles „,“ ja punkt): '
           'seeria andmed, löökide tulemused ja kõigi löökide toorandmed (aeg ms, kiirendus g).'))
S.append(figs_side([('raport_lk1.png', 'PDF-raport, lk 1: koondtulemus ja löökide tabel.'),
                    ('raport_lk2.png', 'PDF-raport, lk 2: impulsside graafikud.')], 80))

# ---------------------------------------------------------------- 10
S.append(H1('10. Püsivara uuendamine'))
S += steps([
    'Laadi telefoni või arvutisse uusim <b>firmware.bin</b>: github.com/eplik82/shock_test → Releases.',
    'Ühenda seadme WiFi-ga ja ava portaal brauseris (nupp „Ava brauseris“).',
    'Vali vaheleht <b>Püsivara</b> → <b>Vali fail</b> → firmware.bin → <b>Laadi üles</b>.',
    'Hoia telefoni ekraan sees ja brauser esiplaanil. Fail saadetakse 32 KB tükkidena; edenemine on näha nii '
    'brauseris kui seadme ekraanil (ca 2–3 minutit).',
    'Seade taaskäivitub. Kui uus versioon ei tööta 20 sekundi jooksul korrektselt, taastatakse automaatselt eelmine '
    'versioon ja ekraanil kuvatakse teade.'])
S.append(note('Seaded ja salvestatud testid säilivad uuendamisel. Uuendamise ajaks lõpetatakse lööki ootamine.'))

# ---------------------------------------------------------------- 11
S.append(PageBreak())
S.append(H1('11. Veaotsing'))
S.append(table([
    ['Probleem', 'Lahendus'],
    ['Kollane rida „Kell seadistamata“', 'Ühenda telefon seadme WiFi-ga ja ava portaal – kell sünkroniseeritakse automaatselt.'],
    ['„praegu … (SIMULATSIOON)“, andur ei vasta', 'Kontrolli anduri ühendust (Vin, GND, SDA, SCL) ja lülita seade välja/sisse. '
                                                 'Portaali „Seade“ vahelehel peab olema „ühendatud“.'],
    ['Puutetundlikkus ei tööta', 'Vigane anduri ühendus võib hoida I2C siini kinni – eemalda andur ja käivita seade uuesti.'],
    ['Telefonis valge leht', 'Sulge sisselogimise aken, ühenda WiFi uuesti või ava brauseris http://4.3.2.1.'],
    ['„Vali fail“ ei tööta', 'Sisselogimise aknas see ei tööta – vajuta „Ava brauseris“.'],
    ['Allalaadimine ei käivitu', 'Ava portaal tavalises brauseris; vajadusel lülita mobiilne andmeside ajutiselt välja.'],
    ['Telefon katkestab WiFi', 'Ära sulge sisselogimise akent enne nupu „Ava brauseris“ vajutamist; '
                               'vajadusel vali telefonis „Hoia ühendus“.'],
    ['Püsivara üleslaadimine peatub', 'Hoia ekraan sees; brauser kordab peatunud tükki automaatselt. '
                                      'Vajadusel alusta uuesti.'],
    ['Löök ei käivitu', 'Kontrolli käivitusläve ja et seade on olekus „OOTAB LÖÖKI“ (nupp Valmis).'],
    ['Üks löök annab mitu mõõtmist', 'Suurenda „Ooteaeg pärast lööki“ või kinnita andur jäigemalt.'],
    ['„andur küllastunud“', 'Kiirendus ületas 200 g. Vali A ülempiir 166 g või väiksem A.'],
    ['Must ekraan, taustvalgus põleb', 'Ühenda USB-kaabel lahti ja uuesti (täielik toite katkestus).'],
], [55, 115]))

# ---------------------------------------------------------------- 12
S.append(H1('12. Tehnilised andmed'))
S.append(table([
    ['Parameeter', 'Väärtus'],
    ['Plaat', 'Waveshare ESP32-S3-Touch-LCD-4.3: 800×480 puuteekraan, 16 MB flash, 8 MB PSRAM'],
    ['Andur', 'ADXL375, ±200 g, 49 mg/LSB, I2C 0x53 (400 kHz)'],
    ['Valimisagedus', '1600 Hz (mõõdetud ~1609 Hz); impulsi punkte 11 ms juures ~18, 6 ms juures ~10'],
    ['Mõõtevahemik A', '5 … 200 g (või 166 g)'],
    ['Andmemälu', '~10 MB (sadu seeriaid)'],
    ['WiFi', 'Pääsupunkt 2,4 GHz, WPA2, aadress 4.3.2.1, captive portaal'],
    ['Toide', '5 V USB-C (UART pesa)'],
    ['Lähtekood ja püsivara', 'github.com/eplik82/shock_test'],
], [45, 125]))


def build():
    doc = SimpleDocTemplate(OUT, pagesize=A4, leftMargin=20 * mm, rightMargin=20 * mm, topMargin=18 * mm,
                            bottomMargin=18 * mm, title='Löögitesti seade – kasutusjuhend', author='Threod Systems',
                            subject=f'shock_test v{VER}')
    doc.build(S, onFirstPage=cover, onLaterPages=on_page)
    print('valmis:', os.path.abspath(OUT))


build()
