# [Projet DIY] Lignenouvelle — boîtier sonore Wi-Fi pour réseau miniature

Depuis quelques mois je travaille sur un petit boîtier autonome destiné à animer la bande-son d'un réseau : sons de locomotives, annonces SNCF, portes de TGV, freins, ambiances... le tout commandé depuis un téléphone, sans application à installer.

## Le principe

L'ESP32 se connecte à la box Wi-Fi et sert lui-même une page web. On ouvre `lignenouvelle.local` sur le téléphone, et on a une interface avec toutes les locos et leurs sons. Pas de bouton physique, pas de télécommande, pas d'appairage Bluetooth — juste le navigateur.

Les sons sont organisés en séquences : pour le TGV par exemple, "Cycle de marche" enchaîne démarrage → vitesse lente → accélération → vitesse rapide, avec des boucles qu'on fait avancer au moment voulu. Ça donne quelque chose d'assez vivant.

## Le matériel — moins de 25 €

- ESP32-S3 (carte de développement, ~9 €)
- Amplificateur MAX98357A I2S (~3 €)
- Haut-parleur 4 Ω 3 W (~5 €)
- Carte microSD (~5 €)
- Quelques fils Dupont et un fer à souder

## Les sons

10 locomotives : CC 6500, BB 67400, BB 26000 Sybic, BB 9400, CC 72000, CC 21000, BB 16000, BB 9300, 141 R vapeur et TGV PSE. Sons extraits de projets décodeurs ZIMO et ESU (usage privé).

## Le code est sur GitHub

Tout est documenté, la partie la plus complexe (le moteur audio qui stream depuis la microSD) est commentée.
Lien : https://github.com/GuillaumeLeheuzey/lignenouvelle

---

*[photos + vidéo de quelques séquences TGV à venir]*

Des questions ? Je suis preneur de retours, notamment sur d'autres sources de sons ou d'autres locos à intégrer.
