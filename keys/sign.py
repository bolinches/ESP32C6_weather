import base64
import json
import os
import sys
import hashlib
import re
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization

# --- CONFIGURATION ---
PRIVATE_KEY_PATH = "private.pem"
CONFIG_H_PATH = "../src/config.h"  # Adjusted for standard project structure
BASE_URL = "https://www.colyflor.com/WEATHER/JPA/"

def get_version_from_config():
    """Extracts CURRENT_VERSION from config.h using regex."""
    if not os.path.exists(CONFIG_H_PATH):
        print(f"[WARNING] config.h not found at {CONFIG_H_PATH}. Please check path.")
        return None
    
    try:
        with open(CONFIG_H_PATH, "r") as f:
            content = f.read()
            # Regex to find #define CURRENT_VERSION "x.x"
            match = re.search(r'#define\s+CURRENT_VERSION\s+"([^"]+)"', content)
            if match:
                return match.group(1)
    except Exception as e:
        print(f"[ERROR] Could not read config.h: {e}")
    return None

def sign_firmware(bin_path, version_override=None):
    # --- Validations ---
    if not os.path.exists(PRIVATE_KEY_PATH):
        print(f"[ERROR] Private key '{PRIVATE_KEY_PATH}' not found.")
        return

    if not os.path.exists(bin_path):
        print(f"[ERROR] Binary file '{bin_path}' not found.")
        return

    # Determine Version: Source from config.h, then CLI Param
    version = get_version_from_config()
    if version_override:
        version = version_override
    
    if not version:
        print("[ERROR] Version could not be determined. Aborting.")
        return

    # 1. Load Private Key
    print(f"[PROCESS] Loading Private Key: {PRIVATE_KEY_PATH}")
    with open(PRIVATE_KEY_PATH, "rb") as key_file:
        private_key = serialization.load_pem_private_key(
            key_file.read(), 
            password=None
        )

    # 2. Get Binary Size (for ESP32 Integrity Pre-check)
    bin_size = os.path.getsize(bin_path)

    # 3. Read Binary and Calculate SHA-256 Hash
    print(f"[PROCESS] Calculating SHA-256 Hash for {bin_path}...")
    with open(bin_path, "rb") as f:
        bin_data = f.read()
        bin_hash = hashlib.sha256(bin_data).hexdigest()

    # 4. Create the Handshake String (Version + Hash)
    # The ESP32 recreates this exact string to verify the signature
    handshake = f"{version}|{bin_hash}"
    
    # 5. Sign the Handshake (Produces ASN.1 DER signature)
    print(f"[PROCESS] Signing Handshake: {handshake}")
    signature = private_key.sign(
        handshake.encode('utf-8'),
        ec.ECDSA(hashes.SHA256())
    )

    # Encode signature to Base64 for the JSON manifest
    sig_base64 = base64.b64encode(signature).decode('utf-8')

    # 6. Construct Manifest JSON
    manifest = {
        "version": version,
        "bin_hash": bin_hash,
        "size": bin_size,
        "url": f"{BASE_URL}{os.path.basename(bin_path)}",
        "signature": sig_base64,
        "force": False
    }

    # 7. Write manifest.json
    output_name = "manifest.json"
    with open(output_name, "w") as f:
        json.dump(manifest, f, indent=2)
    
    # --- Final Telemetry ---
    print("\n" + "="*40)
    print(" COLYFLOR SECURE FIRMWARE SIGNER ")
    print("="*40)
    print(f"Target Version : v{version}")
    print(f"Binary File    : {bin_path}")
    print(f"Binary Size    : {bin_size} bytes")
    print(f"SHA-256 Hash   : {bin_hash}")
    print(f"Signature      : {sig_base64[:40]}...")
    print("-" * 40)
    print(f"[SUCCESS] Manifest saved as {output_name}")
    print("="*40 + "\n")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 sign.py <path_to_binary> [version_override]")
        print("Example: python3 sign.py build/firmware.bin")
    else:
        # Check for optional manual version override in command line
        v_override = sys.argv[2] if len(sys.argv) > 2 else None
        sign_firmware(sys.argv[1], v_override)