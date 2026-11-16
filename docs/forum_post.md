# [Projet DIY] Lignenouvelle — boîtier sonore Wi-Fi pour réseau N
<!-- Titre du sujet à copier dans le champ "Titre" du formulaire -->

<!-- Corps du message en BBCode — coller dans l'éditeur du forum -->

Depuis quelques semaines je travaille sur un petit boîtier autonome pour sonoriser mon réseau à l'échelle N.

La solution habituelle — le Digital Sound embarqué dans les locomotives — ne me conviennait pas : trop cher (50 à 100 € par loco équipée), et un résultat sonore limité par la taille du haut-parleur qu'on peut glisser dans une caisse en N.
L'idée ici est différente : un seul boîtier caché dans le décor, avec un vrai haut-parleur, qui joue les sons ambiants de la scène — locomotive qui passe, annonces en gare, portes, freins etc.

[b]Le principe[/b]

Le cœur du projet est un ESP32, un petit microcontrôleur Wi-Fi dans l'esprit de Arduino. Il est relié à deux choses :
[list]
[*]une carte son I2S qui pilote un haut-parleur 3W
[*]un lecteur microSD qui stocke tous les fichiers audio WAV
[/list]

L'ESP32 se connecte à la box Wi-Fi et sert lui-même une page web. On ouvre [i]lignenouvelle.local[/i] sur le téléphone, et on a une interface avec toutes les locos et leurs sons. Pas de bouton physique, pas de télécommande, pas d'appairage Bluetooth — juste le navigateur.

Les sons sont organisés en séquences : pour le TGV par exemple, "Cycle de marche" enchaîne démarrage → vitesse lente → accélération → vitesse rapide, avec des boucles qu'on fait avancer au moment voulu. Ça donne quelque chose d'assez vivant.

[b]Le matériel — moins de 25 €[/b]
[list]
[*]ESP32-S3 (carte de développement, ~9 €)
[*]Amplificateur MAX98357A I2S (~3 €)
[*]Haut-parleur 4 Ω 3 W (~5 €)
[*]Carte microSD (~5 €)
[*]Quelques fils Dupont et un fer à souder
[/list]

[b]Les sons[/b]

10 locomotives : CC 6500, BB 67400, BB 26000 Sybic, BB 9400, CC 72000, CC 21000, BB 16000, BB 9300, 141 R vapeur et TGV PSE. Sons extraits de décodeurs trouvés sur le web.

[b]Le code est sur GitHub[/b]

Tout est documenté, la partie la plus complexe (le moteur audio qui streame depuis la microSD) est commentée.
[url=https://github.com/glhzy/lignenouvelle]https://github.com/glhzy/lignenouvelle[/url]

Le code a été développé avec [b]Claude Code[/b], moyennant un abonnement de 18 €/mois. La partie firmware embarqué (C++, FreeRTOS, I2S, streaming SD) est assez technique pour quelqu'un qui n'est pas développeur... mais l'IA le maîtrise !

[b]Crédits[/b]

Les visuels des locomotives dans l'interface proviennent du site [b]MLG Traffic[/b] : [url=http://www.mlgtraffic.net]http://www.mlgtraffic.net[/url] — une ressource remarquable pour les graphismes de matériel roulant SNCF.

[i][photos + vidéo de quelques séquences TGV à venir][/i]

Des questions ? Je suis preneur de retours, notamment sur d'autres sources de sons ou d'autres locos à intégrer.
