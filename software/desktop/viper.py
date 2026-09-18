import sys
import socket
import subprocess

def send_to_service(command):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(('127.0.0.1', 44332))
        sock.send(command.encode('utf-8'))
        response = sock.recv(1024).decode('utf-8').strip()
        sock.close()
        return response
    except ConnectionRefusedError:
        print("[Error] Viper Background Service is not running or COM port is busy.")
        sys.exit(1)

def print_help():
    print("Viper Biometric CLI")
    print("Usage:")
    print("  viper run <command>   Run a command protected by physical fingerprint.")
    print("  viper ssh-sign <hash> Ask Viper to sign a 32-byte hex hash.")

def main():
    if len(sys.argv) < 2:
        print_help()
        sys.exit(1)

    cmd = sys.argv[1]
    
    if cmd == "run":
        if len(sys.argv) < 3:
            print("Please provide a command to run.")
            sys.exit(1)
            
        target_cmd = " ".join(sys.argv[2:])
        print(f"🔒 GATEKEEPER: Requesting physical fingerprint to authorize: `{target_cmd}`")
        print("Waiting for touch on Viper Key...")
        
        response = send_to_service("GATEKEEPER")
        
        if response == "AUTH:SUCCESS":
            print("✅ Access Granted. Executing command...\n")
            subprocess.run(target_cmd, shell=True)
        else:
            print(f"❌ Access Denied: {response}")
            sys.exit(1)
            
    elif cmd == "ssh-sign":
        if len(sys.argv) < 3:
            print("Please provide a 64-character hex hash.")
            sys.exit(1)
            
        hash_hex = sys.argv[2]
        if len(hash_hex) != 64:
            print("Hash must be exactly 64 hex characters (32 bytes).")
            sys.exit(1)
            
        response = send_to_service(f"SSH_SIGN:{hash_hex}")
        if response.startswith("SIG:"):
            print(response[4:])
        else:
            print(f"FAILED: {response}")
            sys.exit(1)
    else:
        print_help()

if __name__ == "__main__":
    main()
