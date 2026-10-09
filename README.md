# MJ7 Master Chain

Tranche de mastering pour FL Studio (VST3 sur Windows et Mac, Audio Unit sur Mac) : égaliseur, compresseur
3 bandes, compresseur de bus, saturation, image stéréo et limiteur true peak, avec un bouton **ANALYSER** qui
écoute le mix et cale le master sur la cible de diffusion choisie.

## Obtenir le plugin prêt à installer

1. Onglet **Actions** du dépôt : la compilation « Compiler le plugin » démarre à chaque envoi de fichiers
   (10 à 20 minutes).
2. Quand elle est verte, ouvrez **Releases** et téléchargez `MJ7-Master-Chain-Windows.zip` ou `MJ7-Master-Chain-macOS.zip`.
3. Suivez `INSTALLATION.txt`, présent dans chaque archive.

## La chaîne

| # | Module | Rôle |
|---|---|---|
| 1 | Entrée | Niveau envoyé dans la chaîne |
| 2 | EQ | Coupe-sub, basses, bas-médium, présence, air |
| 3 | Multibande | Compression séparée des graves, médiums et aigus (filtres Linkwitz-Riley, somme à plat) |
| 4 | Glue | Compresseur de bus qui soude le mix |
| 5 | Saturation | Lampe, bande ou clipper, suréchantillonnée x4 |
| 6 | Stéréo | Largeur et basses en mono |
| 7 | Limiteur | Limiteur true peak (détection suréchantillonnée x4), plafond réglable |

Latence totale : environ 3 ms, compensée par FL Studio.

## Cibles de diffusion

| Cible | Usage |
|---|---|
| Streaming -14 LUFS | Spotify, YouTube, Deezer, Boomplay, Tidal |
| Apple Music -16 LUFS | Apple Music |
| Radio -12 LUFS | Diffusion radio |
| Fort -10 LUFS | Rap, trap, afro : master compétitif |
| Club -8 LUFS | DJ, sonorisation |
| TV et pub -23 LUFS | Norme EBU R128 (télévision, spots) |

Les plateformes de streaming baissent les morceaux plus forts que leur cible : viser -14 garde la dynamique sans
perdre en volume perçu.

## Le bouton ANALYSER

Il écoute 20 secondes du mix, puis :

- corrige l'équilibre (basses, bas-médium, présence, air) vers le profil du genre choisi, par touches de 3 dB au plus ;
- règle les seuils des compresseurs pour 1,5 à 2 dB de réduction sur les passages forts ;
- passe les graves en mono s'ils sont décorrélés et ajuste la largeur si le mix est trop étroit ou trop large ;
- **simule toute la chaîne sur l'extrait** et cherche le gain du limiteur qui atteint exactement la cible, sans
  dépasser le plafond.

Changer de genre ou de cible après une analyse refait le calcul, sans réécouter. **INTENSITÉ** dose les corrections
d'égalisation, de compression et de stéréo ; le gain du limiteur reste calé sur la cible. **Annuler** revient au
style seul.

## Mesures

LUFS intégré (norme ITU-R BS.1770, portes absolue et relative), court terme, momentané, crête vraie maximale,
dynamique (PLR), réduction du limiteur, corrélation stéréo. **Remise à zéro** relance la mesure intégrée.

## Limites connues

- L'analyse porte sur 20 secondes : sur le morceau entier, le LUFS intégré peut différer de quelques dixièmes.
  Lisez le morceau en entier après une remise à zéro et ajustez avec GAIN.
- Les profils de genre sont des points de départ réglés à la main, pas des normes : jugez à l'oreille.
- Le plugin ne remplace pas un bon mix : un problème de balance entre les instruments se règle au mixage.

## Pour les développeurs

```
cmake -B build -DCMAKE_BUILD_TYPE=Release          # télécharge JUCE 8.0.9
cmake --build build --config Release --target MJ7MasterChain_VST3
```

- `Source/dsp/` : traitements en C++ pur, sans JUCE. Tests : `g++ -std=c++20 -O2 -I Source tests/dsp_tests.cpp -o t && ./t`
- `Source/Params.h` : liste unique des paramètres, des modules et des styles.
- `tests/host_test.cpp` : test d'intégration (`-DMJ7_BUILD_TESTS=ON`, cible `MJ7HostTest`).

Le plugin utilise [JUCE](https://juce.com), sous licence AGPLv3 ou licence commerciale JUCE. Pour un usage
personnel, rien à faire. Si vous distribuez le plugin, il faut soit publier son code source (AGPLv3), soit
prendre une licence JUCE.
