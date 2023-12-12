import select, socket
from datetime import datetime
  
SERVER_ADDR = "192.168.1.10"  
SERVER_PORT = 50000

# Create and bind to the socket. Set it as non-blocking so it can be used with the select statement
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)  
server.setblocking(False)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind((SERVER_ADDR, SERVER_PORT))  
server.listen(5) # Number of unaccepted connections to allow before refusing new connections.

# Lists to store active sockets
socks_in = [server]  
socks_out = []

print("Server Starting...")

try:
    while socks_in:
        # From socket lists get sockets ready for read, write or those with an exception
        read_socks, write_socks, error_socks = select.select(socks_in, socks_out, socks_in)

        for s in read_socks:
            if s is server:
                # Accept new connection
                connection, client_addr = s.accept()
                connection.setblocking(False)
                socks_in.append(connection)
                socks_out.append(connection)
                print("Accepted connection from {}".format(client_addr[0]))

            else:
                # Read incoming data
                data = s.recv(1024)
                if data:
                    print("Received data \"{}\" from {}".format(data.decode(), s.getpeername()[0]))
                else:
                    # Connection closed
                    print("Closed connection from {}".format(s.getpeername()[0]))
                    socks_in.remove(s)
                    if s in socks_out:
                        socks_out.remove(s)
                    s.close()

        for s in write_socks:
            # Send something to the socket
            now = datetime.now()
            now_str = now.strftime("%d/%m/%Y %H:%M:%S")
            s.send(bytes(now_str, "utf-8"))
            socks_out.remove(s)
            print("Sent data \"{}\" to {}".format(now_str, s.getpeername()[0]))
            pass

        for s in error_socks:
            socks_in.remove(s)
            s.close()

except OSError as ex:
    print(ex)
    server.close()

except Exception as ex:
    print(ex)
    server.close()
