#!/usr/bin/env python3
"""Attend que Home Assistant réponde avant de lancer le navigateur du panneau.

Après une coupure de courant, le panneau démarre bien avant Home Assistant : Chromium resterait
sur une page d'erreur. Usage : attendre-ha.py <url> [délai max en s, 600 par défaut]. Rend toujours la
main (code 0) au bout du délai : mieux vaut une page d'erreur qu'un écran noir.
"""
import sys
import time
import urllib.error
import urllib.request


def ha_repond(url, timeout=5):
    try:
        with urllib.request.urlopen(url, timeout=timeout):
            return True
    except urllib.error.HTTPError:
        return True  # HA répond (même 401/404) : le serveur est prêt
    except (OSError, ValueError):
        return False


def main():
    url = sys.argv[1]
    limite = float(sys.argv[2]) if len(sys.argv) > 2 else 600
    debut = time.monotonic()
    while not ha_repond(url):
        if time.monotonic() - debut > limite:
            print("Home Assistant ne répond pas : lancement quand même", file=sys.stderr)
            break
        time.sleep(5)
    return 0


if __name__ == "__main__":
    sys.exit(main())
