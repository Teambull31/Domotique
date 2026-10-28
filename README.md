# Domotique — ESP32 + Home Assistant

## Contenu du dépôt
| Dossier | Rôle |
|---|---|
| [`firmware/`](firmware/README.md) | Nœuds ESP32 en C++ (PlatformIO) : climat, air, énergie, porte, fuite, présence, volets, chauffage — cartes DevKitC, C3, S3 |
| [`homeassistant/`](homeassistant/) | Alertes « tranquillité » pour proches âgés, tableau de bord du proche aidant, tests |
| [`docs/`](docs/) | [Feuille de route](docs/ROADMAP.md), [déploiement chez un proche/client](docs/DEPLOIEMENT.md), [fiche d'installation](docs/INSTALLATION.md), [matériel](docs/MATERIEL.md) |
| `backend/`, `frontend/`, `mosquitto/` | Tableau de bord web autonome (sans Home Assistant), décrit ci-dessous |

---

# Tableau de bord web (optionnel)

Dashboard web temps réel pour visualiser les données de vos ESP32 à distance.

## Architecture

```
ESP32 ──MQTT──► Mosquitto ──► Backend (FastAPI) ──WebSocket──► Frontend (React)
                                     │
                                  SQLite (historique 30j)
```

## Démarrage rapide

### Avec Docker (recommandé)

```bash
cp .env.example .env
docker compose up -d
```

- **Dashboard** : http://localhost
- **API** : http://localhost:8000/api
- **MQTT** : localhost:1883

### En développement

**Backend**
```bash
cd backend
pip install -r requirements.txt
uvicorn main:app --reload
```

**Frontend**
```bash
cd frontend
npm install
npm run dev        # http://localhost:3000
```

**Mosquitto** (Docker)
```bash
docker run -d -p 1883:1883 \
  -v ./mosquitto/config:/mosquitto/config \
  eclipse-mosquitto:2
```

## Format des messages MQTT

### Télémétrie (ESP32 → Serveur)
**Topic :** `home/devices/<device_id>/telemetry`

```json
{
  "room": "salon",
  "temperature": 23.5,
  "humidity": 65.2,
  "light": 450,
  "presence": false,
  "pressure": 1013.2,
  "co2": 650,
  "battery": 87.0,
  "relay": false
}
```
> Tous les champs sont optionnels.

### Statut (Last Will Testament)
**Topic :** `home/devices/<device_id>/status`
**Payload :** `"online"` ou `"offline"`

### Commandes (Serveur → ESP32)
**Topic :** `home/devices/<device_id>/command`

```json
{ "relay": true }
{ "display": "Bonjour!" }
```

## Exemple ESP32

Voir `esp32/example.ino` — bibliothèques nécessaires :
- [PubSubClient](https://github.com/knolleary/pubsubclient)
- [ArduinoJson](https://arduinojson.org/)
- DHT sensor library (Adafruit)

## API REST

| Méthode | Endpoint | Description |
|---------|----------|-------------|
| GET | `/api/devices` | Liste tous les dispositifs |
| GET | `/api/devices/{id}/history/{sensor}?hours=24` | Historique d'un capteur |
| POST | `/api/devices/{id}/command` | Envoyer une commande |
| GET | `/api/health` | Santé de l'API |

## Variables d'environnement

| Variable | Défaut | Description |
|----------|--------|-------------|
| `MQTT_HOST` | `localhost` | Adresse du broker |
| `MQTT_PORT` | `1883` | Port MQTT |
| `MQTT_USER` | `` | Utilisateur (optionnel) |
| `MQTT_PASS` | `` | Mot de passe (optionnel) |
