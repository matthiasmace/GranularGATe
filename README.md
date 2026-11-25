# 🐱 GranularGATe — granulateur granulaire + séquenceur néo-math

Deux livrables dans ce dépôt :

## 1. Démo web interactive (`index.html`)

Séquenceur pas-à-pas sur **deux octaves** couplé à un moteur de **synthèse granulaire** (Web Audio), avec :

- granulation **mono mixdown spatialisée** (formule de panoramique = tournoiement, `sin(2*pi*t*rot)`) ou **stéréo indépendante** (deux granularisations, une par canal)
- **pads de sortie par bandes** de fréquences (basses / médiums / aigus), chacun avec vibrato, glissando ou **formule mathématique** propre
- transformations globales pilotées par des **équations** (Doppler du tournoiement, chaos, transpositions aléatoires…)
- sources internes (cordes / cloche / bruit) ou **ton propre sample**
- motifs par défaut «à la Messiaen» : Regard du Père (fa♯ majeur) ou évocation du Merle Bleu

Fichier autonome : ouvrir `index.html` dans un navigateur (React et Tailwind chargés via CDN, connexion internet requise).

### Partage via GitHub Pages

1. Settings → Pages → *Build and deployment* → Branch : `main` / dossier `/ (root)`
2. La démo est alors servie sur `https://matthiasmace.github.io/granulargate/`

## 2. Greffon LV2 « Granuform » pour Ardour (`granuform.lv2/`)

Greffon audio natif (C) : il granule tout ce qui passe dans la piste où il est inséré.

```bash
cd granuform.lv2
make            # prérequis : gcc + lv2-dev
make install    # copie dans ~/.lv2/granuform.lv2/
```

Puis relancer Ardour : le greffon **Granuform** apparaît dans la liste. Paramètres : densité, taille de grain, dispersion, mode mono-stéréo, rotation avec Doppler, vibrato, glissando (échantillon par échantillon), chaos, mix.

Astuce Schaeffer : une boucle courte sur la piste devient une matière granulaire infinie ; Dispersion à 0 = freeze serré, à 1 = nuage mouvant.

## Licences et crédits

Code : MIT (voir `LICENSE`), Matthias Macé. Écrit avec l'aide de son assistant félin.
