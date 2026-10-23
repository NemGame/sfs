const net = require('net');
const readline = require('readline');
class SFS {
    static RECONNECT_INTERVAL = 2000;
    constructor(host, port, { log = console.log, error = console.error } = {}) {
        this.log = log;
        this.error = error;
        this.host = host;
        this.port = port;
        this.connection = null;
        this.rl = null;
        this.queue = [];
    }
    connect() {
        if (this.connection) {
            this.connection.destroy();
            this.connection = null;
            if (this.rl) {
                this.rl.close();
                this.rl = null;
            }
        }
        this.connection = net.createConnection({ host: this.host, port: this.port });

        this.rl = readline.createInterface({input: this.connection});
        this.rl.on("error", () => {});

        const failPending = (err) => {
            const pending = this.queue;
            this.queue = [];
            pending.forEach(p => p.reject(err));
        };

        this.rl.on("line", (line) => {
            if (this.queue.length > 0) {
                const pending = this.queue.shift();
                pending.resolve(line);
            }
        });

        this.connection.on("connect", () => {
            this.log("SFS", "SFS connection established");
        });

        this.connection.on('error', (err) => {
            this.error("SFS", "SFS connection error:", err.message);
            failPending(err);
            this.reconnect();
        });

        this.connection.on('close', () => {
            this.error("SFS", "SFS connection closed... Reconnecting...");
            failPending(new Error("SFS connection closed"));
            this.reconnect();
        });
    }
    reconnect() {
        if (this.reconnecting) return;
        this.reconnecting = true;
        setTimeout(() => {
            this.reconnecting = false;
            this.connect();
        }, SFS.RECONNECT_INTERVAL);
    }
    async send(command) {
        return new Promise((resolve, reject) => {
            if (!this.connection || this.connection.destroyed || !this.connection.writable) {
                reject(new Error("No connection"));
                return;
            }
            this.queue.push({ resolve, reject });
            this.connection.write(command);
        });
    }
}
module.exports = { SFS };
