const express = require("express");
const fs = require("fs");
const path = require("path");
const readline = require("readline");
const cors = require("cors");
const app = express();
const { spawn, exec } = require('child_process');
const { createProxyMiddleware } = require('http-proxy-middleware');
const { WebSocketServer, WebSocket } = require('ws');

app.use(cors());

app.use(express.json());
const subfilesys = path.join(__dirname, "subfilesys", "subfilesys");

const SFS = require("./sfs");
const sfs = new SFS.SFS("127.0.0.1", 6969, { log, error });
sfs.connect();

app.get("/api/space", (req, res) => {
    log(req.headers["x-forwarded-for"], "\n============Space=Request============");
    sfs.send("SPACE").then(space => {
        res.status(200).send({space});
    }).catch(err => {
        error(`Error getting space: ${err.message}`);
        res.status(500).send(`Error: ${err.message}`);
    });
});

// {"name":"Alya","source":"/mnt/sda/public/","root":"/mnt/sda","path":"","fullpath":"/Alya","isDirectory":true,"extension":"","size":"573.92 MB"}
// {"name":"Alya","isDirectory":true}
app.get("/api/files", (req, res) => {
    log(req.headers["x-forwarded-for"], "\n===========Files=Request===========");
    const dir = req.query.dir ? normalizePath(req.query.dir) : "/";
    const key = req.query.key || "default";
    log(req.headers["x-forwarded-for"], "Requesting contents in directory: " + dir);
    sfs.send(`P${dir}:${key}`).then(contentsStr => {
        //const json = JSON.parse(contents);
        let contents = {};
        try {
            contents = JSON.parse(contentsStr);
        } catch (err) {
            error(`Error parsing contents: ${contentsStr}\n${err.message}`);
        }
        res.status(200).json(contents);
    }).catch(err => {
        error(`Error getting files: ${err.message}`);
        res.status(500).send(`Error: ${err.message}`);
    });
});

// Structure
// Folders - present in every root\publicfoldername, so all have same structure
// Files - different in each root\publicfoldername, root1\publicfoldername may have fileA, root2\publicfoldername may not
// Adding a folder adds it to all roots\publicfoldername
// Listing reads from all roots\publicfoldername
// Downloading reads from the first root\publicfoldername that has the file (since it's impossible to have same file in multiple roots\publicfoldername)
// Adding a file: calculates which root\publicfoldername has the most free space, adds it there


const publicfoldername = "public";

const searchRoots = [
    "/mnt"
];

app.get("/api/download", (req, res) => {
    // Unimplemented
    return res.status(501).send("Download not implemented");
	log(req.headers["x-forwarded-for"], "\n===========Download=Request===========");
	const cpath = normalizePath(req.query.path);
    if (!cpath) return res.status(400).send("Missing path.");
	log(req.headers["x-forwarded-for"], "Requested download for path: " + cpath);
    const fullPath = fullPathOf(cpath, true);
    if (!fullPath || !fs.existsSync(fullPath)) {
		error(`Non-existant download request: ${cpath}`);
        return res.status(404).send("File not found");
    }
    fs.stat(fullPath, (err, stats) => {
        if (err || !stats.isFile()) {
            error(`File not found or error: ${fullPath}`);
            return res.status(404).send("File not found");
        }

        const fileName = path.basename(fullPath);
        log(req.headers["x-forwarded-for"], `Starting stream for: ${fileName} (${stats.size} bytes)`);

        // 2. Set headers
        res.writeHead(200, {
            'Content-Length': stats.size,
            'Content-Type': 'application/octet-stream',
            'Content-Disposition': `attachment; filename="${encodeURIComponent(fileName)}"`,
            'X-Content-Type-Options': 'nosniff'
        });

        // 3. Create stream with a specific highWaterMark (buffer size) 
        // 64KB is usually sweet for Raspberry Pi USB overhead
        const fileStream = fs.createReadStream(fullPath, { highWaterMark: 64 * 1024 });

        fileStream.pipe(res);

        fileStream.on('error', (streamErr) => {
            error("Stream error:", streamErr);
            if (!res.headersSent) res.status(500).send("Stream failed");
            else res.end();
        });

        // Cleanup if user cancels download
        req.on('close', () => {
            fileStream.destroy();
        });
    });
});

// Like download
// Satisfies <script src="drive.ngakos.lol/api/server/-filepath-.js"></script>
app.get("/api/serve", (req, res) => {
    return res.status(501).send("Serve not implemented");
    log(req.headers["x-forwarded-for"], "\n=============Serve=Request============");
    const cpath = normalizePath(req.query.path || "");
    if (!cpath) return res.status(400).send("Missing path.");

    log(req.headers["x-forwarded-for"], "Requested serve for path: " + cpath);
    const fullPath = fullPathOf(cpath, true);
    if (!fullPath || !fs.existsSync(fullPath)) {
        error(`Non-existant serve request: ${cpath}`);
        return res.status(404).send("File not found");
    }

    fs.stat(fullPath, (err, stats) => {
        if (err || !stats.isFile()) {
            error(`File not found or error: ${fullPath}`);
            return res.status(404).send("File not found");
        }

        const extension = path.extname(fullPath).toLowerCase();
        const mimeByExtension = {
            ".js": "application/javascript; charset=utf-8",
            ".mjs": "application/javascript; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".html": "text/html; charset=utf-8",
            ".json": "application/json; charset=utf-8",
            ".txt": "text/plain; charset=utf-8",
            ".svg": "image/svg+xml"
        };

        res.writeHead(200, {
            "Content-Length": stats.size,
            "Content-Type": mimeByExtension[extension] || "application/octet-stream",
            "Content-Disposition": "inline",
            "Cache-Control": "public, max-age=300",
            "X-Content-Type-Options": "nosniff"
        });

        const fileStream = fs.createReadStream(fullPath, { highWaterMark: 64 * 1024 });
        fileStream.pipe(res);

        fileStream.on("error", (streamErr) => {
            error("Stream error:", streamErr);
            if (!res.headersSent) res.status(500).send("Stream failed");
            else res.end();
        });

        req.on("close", () => {
            fileStream.destroy();
        });
    });
});

app.get("/api/add", (req, res) => {
    return res.status(501).send("Add not implemented");
    log(req.headers["x-forwarded-for"], "\n==============Add=Request=============");
    const fpath = normalizePath(req.query.path), type = req.query.type;
    if (!fpath) return res.status(400).send("Missing path.");
    if (!type) return res.status(400).send("Missing type. (directory/file)");
    log(req.headers["x-forwarded-for"], `Requested to add ${type} at path: ${fpath}`);
    if (type === "directory") {
        addFolder(req.id, fpath);
        return res.status(200).json({status: "success", message: `Directory added at ${fpath}`});
    } else {
        // run ./subfilesys/subfilesys -addfile <fpath>
        const { exec } = require("child_process");
        let errorg = "", stderrg = "";
        exec(`"${subfilesys}" -addfile "${fpath}"`,         (execErr, stdout, stderr) => {
                    if (execErr) {
                        error(`Error executing fix command: ${execErr.message}`);
                        errorg = execErr.message;
                return;
            }
            if (stderr) {
                error(`Fix command stderr: ${stderr}`);
                stderrg = stderr;
                return;
            }
            log(req.headers["x-forwarded-for"], stdout, "\n");
            res.status(200).send({status:200, message: "Done"});
        });
        if (errorg || stderrg) res.status(400).send(errorg + "\n\n" + stderrg);
    }
});

app.post("/rename", (req, res) => {
    return res.status(501).send("Rename not implemented");
    log(req.headers["x-forwarded-for"], "\n===========Rename=Request===========");
    const { oldpath, newname } = req.body;
    const noldpath = normalizePath(oldpath || "");
    if (!noldpath || noldpath === "/") {
        log(req.headers["x-forwarded-for"], "The oldpath was not provided");
        return res.status(400).send("Missing oldpath");
    }
    if (!newname) {
        log(req.headers["x-forwarded-for"], "The newname was not provided");
        return res.status(400).send("Missing new name." + newname);
    }
    log(req.headers["x-forwarded-for"], `Requested to rename "${noldpath}" to "${newname}"`);
    const fullPath = fullPathOf(noldpath, true);
    if (!fullPath || !fs.existsSync(fullPath)) {
        error(`Non-existant rename request: ${noldpath}`);
        return res.status(404).send("File not found");
    }
    fs.rename(fullPath, path.join(path.dirname(fullPath), newname), (err) => {
        if (err) {
            error(`Error renaming file: ${err.message}`);
            return res.status(500).send(`Error: ${err.message}`);
        }
        log(req.headers["x-forwarded-for"], `Renamed ${noldpath} to ${newname}`);
        res.status(200).send({status: "success", message: `Renamed ${noldpath} to ${newname}`});
    });
});

app.get("/api/fix", (req, res) => {
    return res.status(501).send("Fix not implemented");
    log(req.headers["x-forwarded-for"], "\n===========Fixing=Structure==========");
    fixFolderStructure();
    res.json({status: "success", message: "Fix command executed."});
});

let uploadKeys = {};
// Returns {key:number, id:number} tied to the filename
app.get("/api/requestkey/:filename", (req, res) => {
    return res.status(501).send("Request key not implemented");
    const filename = req.params.filename;
    const key = Math.floor(Math.random() * 0xFF);
    const id = Date.now();
    uploadKeys[key] = {id, filename};
    res.json({key, id});
    log(req.headers["x-forwarded-for"], `Requested upload key for ${filename} - Key: ${key}, ID: ${id}`);
    setTimeout(() => {
        delete uploadKeys[key];
    }, 5 * 60 * 1000); // Expire after 5 minutes
});

app.get("/api/safeupload", (req, res) => {
    return res.status(501).send("Safe upload not implemented");
    log(req.headers["x-forwarded-for"], "\n===========SafeUpload=Request===========");
    const key = req.query.key;
    const id = req.query.id;

    const filename = req.headers['x-file-name'];
    if (!key || !id) {
        error("Missing key or id in safe upload request.");
        return res.status(400).send("Missing key or id.");
    }
    if (filename !== uploadKeys[key]?.filename || uploadKeys[key].id != id) {
        error(`Invalid key or id for safe upload. Key: ${key}, ID: ${id}, Filename: ${filename}`);
        return res.status(403).send("Invalid key or id.");
    }
    delete uploadKeys[key];
    log(req.headers["x-forwarded-for"], `Safe upload authorized for ${filename} with key ${key} and id ${id}. Proceeding with upload.`);

    const flipper = new Transform({
        transform(chunk, encoding, callback) {
            for (let i = 0; i < chunk.length; i++) {
                chunk[i] = chunk[i] ^ key;
            }
            callback(null, chunk);
        }
    });

    const subProcess = spawn(subfilesys, ['-upload', fpath, '-filename', filename, '-uploadsize', req.headers['x-file-size']]);
    req.on('aborted', () => {
        log(req.headers["x-forwarded-for"], `Client aborted safe upload for ${filename}`);
        subProcess.kill();
    });
    subProcess.stdout.on('data', (data) => log(req.headers["x-forwarded-for"], `C++ STDOUT: ${data}`));
    subProcess.stderr.on('data', (data) => error(`C++ STDERR: ${data}`));
    subProcess.on('error', (err) => {
        error(`SUBPROCESS ERROR:`, err);
        if (!res.headersSent) {
            res.status(500).json({ error: "External tool failed to start." });
        }
    });
    subProcess.on('close', (code) => {
        log(req.headers["x-forwarded-for"], `Subprocess exited with code ${code}`);
        if (!res.headersSent) {
            res.json({ status: 'done', code });
        }
    });

    req.pipe(flipper).pipe(subProcess.stdin);
    subProcess.stdin.on('error', (err) => {
        error(`Pipe to C++ failed: ${err.message}`);
        res.status(500).send("Error saving file.");
    });

    subProcess.stdin.on('finish', () => {
        log(req.headers["x-forwarded-for"], `Safe upload completed for ${filename}. File processed by C++ tool.`);
    });

    fileStream.on('finish', () => {
        log(req.headers["x-forwarded-for"], `Safe upload completed for ${filename}. File saved to ${savePath}`);
        res.status(200).send("Upload successful.");
        delete uploadKeys[key];
    });
    
    fileStream.on('error', (err) => {
        error(`Error saving uploaded file: ${err.message}`);
        res.status(500).send("Error saving file.");
    });
});

let uploadID = Number.MIN_SAFE_INTEGER;
let uploadIDs = new Set();
app.post("/api/upload", (req, res) => {
    return res.status(501).send("Upload not implemented");
    const ID = ++uploadID;
    if (uploadID > Number.MAX_SAFE_INTEGER) uploadID = Number.MIN_SAFE_INTEGER;

    try {
        log(req.headers["x-forwarded-for"], "\n============Upload=Request===========");
        
        // Use standard header casing or the casing you set in XHR
        const filename = req.headers['x-file-name']; 
        const totalSize = req.headers['content-length'];
        const path = req.query.path || "";
        const fpath = normalizePath(path);

        uploadIDs.add(ID);
        log(req.headers["x-forwarded-for"], `ID: ${ID}\nSize: ${totalSize} bytes\nPath: ${path}\nFilename: ${filename}`);

        // Spawn the C++ process
        const subProcess = spawn(subfilesys, ['-upload', fpath, '-filename', filename, '-uploadsize', totalSize]);

        // PIPE DIRECTLY: req -> subProcess.stdin
        req.pipe(subProcess.stdin);

        let gotError = false;

        subProcess.stdout.on('data', (data) => log(req.headers["x-forwarded-for"], `C++ STDOUT: ${data}`));
        subProcess.stderr.on('data', (data) => error(`C++ STDERR: ${data}`));
        
        subProcess.stdin.on('error', (err) => {
            error(`Pipe to C++ failed (ID: ${ID}). C++ likely exited early.\n${err}`);
            gotError = true;
        });

        subProcess.on('error', (err) => {
            error(`SUBPROCESS ERROR (ID: ${ID}):`, err);
            if (!res.headersSent) {
                res.status(500).json({ error: "External tool failed to start." });
            }
            gotError = true;
        });

        subProcess.on('close', (code) => {
            log(req.headers["x-forwarded-for"], `Subprocess ${ID} exited with code ${code}`);
            if (!gotError) {
                uploadIDs.delete(ID);
                log(req.headers["x-forwarded-for"], `Upload completed without any errors: ${ID}`);
            }
            if (!res.headersSent) {
                res.json({ status: 'done', code });
            }
        });

        // If the client cancels the upload, kill the C++ process
        req.on('aborted', () => {
            log(req.headers["x-forwarded-for"], `Client aborted upload ${ID}`);
            gotError = true;
            subProcess.kill();
        });

    } catch (err) {
        error(`ERROR: upload\nID: ${ID}\n${err.message}`);
        if (!res.headersSent) res.status(500).send("Internal Server Error");
    }
});

app.get("/api/delete", (req, res) => {
    return res.status(501).send("Delete not implemented");
    log(req.headers["x-forwarded-for"], "\n===========Delete=Request===========");
    const fpath = req.query.path, type = req.query.type;
    if (!fpath) return res.status(400).send("Missing path.");
    if (!type) return res.status(400).send("Missing type. (directory/file)");
    log(req.headers["x-forwarded-for"], `Requested to delete ${type} at path: ${fpath}`);
    // Call subfilesys -type type -delete path
    exec(`"${subfilesys}" -type ${type} -delete "${fpath}"`, (err, stdout, stderr) => {
        if (err) {
            error(`Error executing delete command: ${err.message}`);
            return res.status(500).send(`Error: ${err.message}`);
        }
        if (stderr) {
            error(`Delete command stderr: ${stderr}`);
            return res.status(500).send(`Error: ${stderr}`);
        }
        log(req.headers["x-forwarded-for"], stdout, "\n");
        res.status(200).send({status: "success", message: `Deleted ${type} at ${fpath}`});
    });
});

app.get("/api/full", (req, res) => {
    return res.status(501).send("Full path not implemented");
    log(req.headers["x-forwarded-for"], "\n===========FullPath=Request===========");
    const cpath = normalizePath(req.query.path) || "";
    log(req.headers["x-forwarded-for"], "Requested full path for: " + cpath);
    const fullPath = fullPathOf(cpath, true);
    if (!fullPath) {
        error(`Non-existant full path request: ${cpath}`);
        return res.status(404).json({error: "Nope"});
    }
    res.json({fullPath: fullPath});
});

app.get("/api/space/json", (req, res) => {
    return res.status(501).send("Space not implemented");
	log(req.headers["x-forwarded-for"], "\n============Space=Request============");
	exec(`"${subfilesys}" -space`, (err, stdout, stderr) => {
		if (err) {
			error(`Error executing space command: ${err.message}`);
			return res.status(500).send(`Error: ${err.message}`);
		}
		if (stderr) {
			error(`Space command stderr: ${stderr}`);
			return res.status(500).send(`Error: ${stderr}`);
		}
		log(req.headers["x-forwarded-for"], stdout, "\n");
		res.status(200).send("");
	});
});

//#region Appdata

app.get("/api/app/:appName", (req, res) => {
    const appName = req.params.appName.trim();
    console.log(`Requested appdata for "${appName}"`);
    if (!appdata[appName]) {
        console.log("FUCK");
        return res.status(400).json({ message: "App not found", shit: appdata});
    }
    console.log("Found");
    const data = appdata[appName];
    return res.status(200).json(data);
});

app.post("/api/appdata", (req, res) => {
    ReloadAppdata();
    return res.status(200);
});

fs.watch("appdata.txt", (eventType, filename) => {
    ReloadAppdata();
    log("FileWatch", "Reloaded appdata", "\n");
});
//#endregion

app.get("/api/refeet", (req, res) => {
    exec("sudo reboot now");
    res.status(200).send("Rebooting...");
	log(req.headers["x-forwarded-for"], "Refeet");
});

app.get("/api/hamismarcipanszelet", (req, res) => {
    exec("sudo shutdown now");
    res.status(200).send("Yummers");
	log(req.headers["x-forwarded-for"], "Hamismarcipanszelet");
});

app.get("/api/wakeywakey", (req, res) => {
	if (!req.query.just5mins) return;
	exec(`sudo rtcwake -m off -s ${req.query.just5mins}`);
	res.status(200).send("Get up!");
	log(req.headers["x-forwarded-for"], `WakeyWakey - ${req.query.just5mins}`);
});

app.get("/api/key", (req, res) => {
    if (!req.query.password) return res.status(400).send("");
    return res.status(200).send(encrypt(req.query.password));
});

app.get("/api/alive", (req, res) => {
    return res.status(200).send("Alive");
});

let hosted = {}; // path:str{port:num, process:ChildProcess}

app.get("/host", (req, res) => {
    return res.status(501).send("Host not implemented");
    log(req.headers["x-forwarded-for"], "\n===========Host=Request===========");
    const dir = req.query.dir;
    if (!dir) {
        return res.status(400).send("Missing directory");
    }
    const fullPath = fullPathOf(normalizePath(dir));
    if (!fullPath || !fs.existsSync(fullPath)) {
        return res.status(404).send("Directory not found");
    }
    if (!fs.statSync(fullPath).isDirectory()) {
        return res.status(400).send("Not a directory");
    }
    if (fullPath in hosted) {
        return res.status(400).send("Already running on port: " + hosted[fullPath].port);
    }
    // Host the directory with Python's http.server on a random port between 8000 and 9000, and return the port
    const port = Math.floor(Math.random() * (9000 - 8000 + 1)) + 8000;
    const pythonProcess = spawn("python3", ["-m", "http.server", port, "--bind", "127.0.0.1", "--directory", fullPath]);
    pythonProcess.stdout.on('data', (data) => log(req.headers["x-forwarded-for"], `Python STDOUT: ${data}`));
    pythonProcess.stderr.on('data', (data) => error(`Python STDERR: ${data}`));
    pythonProcess.on('error', (err) => {
        error(`Python process error:`, err);
        if (!res.headersSent) {
            res.status(500).json({ error: "Failed to start hosting." });
        }
    });
    hosted[fullPath] = {port:port, process:pythonProcess};
    res.send(`<a href="https://drive.ngakos.lol/host/j/${port}">Hosted Link</a> (Port: ${port})`);
});

app.use(/\/host\/j\/.*/, (req, res, next) => {
    return res.status(501).send("Proxy not implemented");
    const port = req.originalUrl.split("/")[3];
    if (!Object.values(hosted).some(x => x.port == port)) res.status(400).send("No server is running on port: " + port);
    log(req.headers["x-forwarded-for"], `Proxying request to hosted directory on port ${port} ; ${req.originalUrl}`);
    // Dinamikusan létrehozunk egy proxy-t az adott porthoz
    const proxy = createProxyMiddleware({
        target: `http://127.0.0.1:${port}`,
        changeOrigin: true,
        router: (req) => {
            // Kinyerjük a portot az URL-ből dinamikusan
            const port = req.originalUrl.split("/")[3];
            log("x", "URL: " + `http://127.0.0.1:${port}\nOriginal URL: ${req.originalUrl}`);
            return `http://127.0.0.1:${port}`;
        },
        pathRewrite: (path, req) => {
            // Kicseréljük a /host/j/SZÁM részt semmire, de a többit meghagyjuk!
            // A [0-9]+ biztosítja, hogy csak a portot vágjuk le
            const cleanedPath = path.replace(/^\/host\/j\/[0-9]+/, '');
            log("x", "Cleaned: " + cleanedPath === '' ? '/' : cleanedPath);
            return cleanedPath === '' ? '/' : cleanedPath;
        },
        proxyTimeout: 5000, 
        timeout: 5000,
        onError: (err, req, res) => {
            log(req.headers["x-forwarded-for"], `Proxy hiba: ${err.message}`);
            if (!res.headersSent) {
                res.status(502).send("A belső szerver (Python) nem érhető el ezen a porton.");
            } else res.status(502).send("A belső Python szerver nem válaszol.");
        },
        logLevel: 'debug'
    });

    return proxy(req, res, next);
});

app.use("/host/a", (req, res) => {
    log(req.headers["x-forwarded-for"], "\n===========HostedList=Request===========");
    let o = [];
    Object.keys(hosted).forEach(key => {
        o.push({path:key, port:hosted[key].port});
    });
    res.status(200).json(o);
});

function encrypt(password="") {
    let lastChar = 0;
    const charmap = password.split("").map(x => x.charCodeAt(0)).map((x, i) => {
        let y = x^(8 + i**3 + lastChar);
        lastChar = x;
        return y;
    });
    return charmap.map(x => String.fromCharCode(x)).join("");
}

function decrypt(encrypted="") {
    let lastChar = 0;
    const charmap = encrypted.split("").map(x => x.charCodeAt(0)).map((x, i) => {
        let y = x^(8 + i**3 + lastChar);
        lastChar = y;
        return y;
    });
    return charmap.map(x => String.fromCharCode(x)).join("");
}





fs.mkdirSync(path.join(__dirname, "logs"), { recursive: true });
const logfile = path.join(__dirname, "logs", `server${Date.now()}.log`);

//#region latest.log
const symlinkPath = path.join(__dirname, "logs", "latest.log");
try {
    if (fs.existsSync(symlinkPath)) {
        fs.unlinkSync(symlinkPath);
    }

    fs.symlinkSync(logfile, symlinkPath);
    log("System", "latest.log updated");
} catch (err) {
    error("Error while updating latest.log:", err.message);
}
//#endregion

// console.log and write to log file
function log(ip=null, ...args) {
    try {
        const logMessage = args.join(" ");
        console.log(logMessage);
        const date = new Date();
        const fileMessage = `[${ip ? ip : "unknown"}][${date.toISOString()}] ${logMessage}`;
        fs.appendFileSync(logfile, fileMessage + "\n");
    } catch {
        console.error("ERROR UPDATING LOG - LOG");
    }
}

function error(...args) {
    try {
        const errorMessage = args.join(" ");
        console.error(errorMessage);
        const date = new Date();
        const fileMessage = `[ERROR][${date.toISOString()}] ${errorMessage}`;
        fs.appendFileSync(logfile, fileMessage + "\n");
    } catch {
        console.error("ERROR UPDATING LOG - ERROR");
    }
}

function fastDirSize(dirPath) {
	let totalSize = 0;
	const files = fs.readdirSync(dirPath, { withFileTypes: true });
	files.forEach(file => {
		if (file.isDirectory()) {
			totalSize += fastDirSize(path.join(dirPath, file.name));
		} else {
			totalSize += fs.statSync(path.join(dirPath, file.name)).size;
		}
	});
	return totalSize;
}


function fullPathOf(fpath, debug=false) {
	// Search all roots / publicfoldername and return the first instance found
    let fullPath = "";
	if (debug) log("fullpath", `Searching for full path of: ${fpath}`);
	searchRoots.forEach((root) => {
		if (debug) log("fullpath", "Found root: " + root);
		if (fullPath) return;
		try {
			if (fs.existsSync(root)) {
				if (root.endsWith(publicfoldername)) {
					if (debug) log("fullpath", `Found ${publicfoldername} in root: ${root}`);
                    if (fs.existsSync(path.join(root, fpath))) fullPath = path.join(root, fpath);
				} else {
					const drives = fs.readdirSync(root);
					drives.forEach(drive => {
						if (debug) log("fullpath", `Searching drive: ${drive}`);
						const drivePublicPath = path.join(root, drive, publicfoldername);
						if (fs.existsSync(drivePublicPath)) {
							if (debug) log("fullpath", `Found ${publicfoldername} in drive: ${drivePublicPath}`);
                            if (fs.existsSync(path.join(drivePublicPath, fpath))) fullPath = path.join(drivePublicPath, fpath);
						}
					});
				}
			}
		} catch (err) {
			error(`Error scanning ${root}:`, err.message);
		}
	});
	if (debug) log("fullpath", `Path: ${fpath} -> ${fullPath}`);
    return fullPath;
}

function addFolder(ip, fpath) {
	// Add folder to all roots
    searchRoots.forEach((root) => {
		if (root.endsWith(publicfoldername)) {
			if (fs.existsSync(path.join(root, fpath))) {
                log(ip, `Folder already exists at: ${path.join(root, fpath)}`);
				return;
			}
			fs.mkdirSync(path.join(root, fpath), { recursive: true });
			log(ip, `Folder added at: ${path.join(root, fpath)}`);
		} else {
            const drives = fs.readdirSync(root);
			drives.forEach(drive => {
                const drivePublicPath = path.join(root, drive, publicfoldername);
				if (fs.existsSync(drivePublicPath)) {
                    if (fs.existsSync(path.join(drivePublicPath, fpath))) {
                        log(ip, `Folder already exists at: ${path.join(drivePublicPath, fpath)}`);
                        return;
                    }
					fs.mkdirSync(path.join(drivePublicPath, fpath), { recursive: true });
					log(ip, `Folder added at: ${path.join(drivePublicPath, fpath)}`);
				}
			});
		}
	});
}

function fixFolderStructure() {
    return;  // TODO: add fix
    // Run command "./subfilesys/subfilesys -fix"
    exec(`"${subfilesys}"     -fix`, (execErr, stdout, stderr) => {
            if (execErr) {
                error(`Error executing fix command: ${execErr.message}`);
            return;
        }
        if (stderr) {
            error(`Fix command stderr: ${stderr}`);
            return;
        }
        log("fix", stdout, "\n");
    });
}

function normalizePath(path) {
    const parts = path.split("/").filter(part => part.length > 0);
    const stack = [];
    parts.forEach(part => {
        if (part === "..") {
            stack.pop();
        } else if (part !== ".") {
            stack.push(part);
        }
    });
    return ("/" + stack.join("/") + (path.endsWith("/") ? "/" : "")).replace("//", "/");
}

function formatBytes(bytes) {
	const types = ["KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB", "BB", "NB", "DB", "CB"];
    let i = -1;
    while (bytes >= 1024 && ++i < types.length) bytes /= 1024;
    return `${bytes.toFixed(2)}${" " + (types[i] || "B")}`;
}

function readDirSafe(dirPath, list, getsize=true, allowDuplicates=false) {
	const files = fs.readdirSync(dirPath, { withFileTypes: true });
	const split = dirPath.split("/");
	const localpath = split.slice(4).join("/");
	files.forEach(file => {
        const f = {
			name: file.name,
			source: dirPath,
			root: split.slice(0, 3).join("/"),
			path: localpath,
			fullpath: localpath + "/" + file.name,
			isDirectory: file.isDirectory(),
			extension: path.extname(file.name),
			size: getsize ? formatBytes(file.isDirectory() ? fastDirSize(path.join(dirPath, file.name)) : fs.statSync(path.join(dirPath, file.name)).size) : 0
		};
        if (!allowDuplicates && list.find(item => item.name === f.name && item.isDirectory === f.isDirectory)) return;
		list.push(f);
	});
}

let appdata = {};
async function ReloadAppdata() {
    appdata = await parseAppData("appdata.txt");
}
async function parseAppData(fileName) {
    // A path.join segít, hogy az appdata.txt biztosan a megfelelő mappából olvasódjon be
    const filePath = path.join(__dirname, fileName);
    const fileStream = fs.createReadStream(filePath);
    
    const rl = readline.createInterface({
        input: fileStream,
        crlfDelay: Infinity // Kezeli a Windows (\r\n) és Linux (\n) sorvégeket is
    });
    
    const result = {};
    let currentGame = null;
    
    for await (const line of rl) {
        const trimmedLine = line.trim();
        
        // Üres sorok átugrása
        if (!trimmedLine) continue;
        
        // Ha a sor a játék nevét jelzi (azzal kezdődik, hogy "? ")
        if (trimmedLine.startsWith('? ')) {
            currentGame = trimmedLine.replace('? ', '').trim();
            result[currentGame] = {};
        } 
        // Ha kulcs-érték pár (tartalmaz "=" jelet) ÉS már van aktív játékunk
        else if (trimmedLine.includes('=') && currentGame) {
            const [key, value] = trimmedLine.split('=').map(str => str.trim());
            result[currentGame][key] = value;
        }
    }
    
    console.log(result);
    console.log("Keys:");
    console.log(Object.keys(result));

    return result;
}
const sitePath = path.resolve(__dirname, "site");
app.use(express.static(sitePath));
log("startup", "Static files being served from:", sitePath);

app.get(/.*/, (req, res) => {
	res.sendFile(path.join(__dirname, "site", "index.html"));
});

app.set("trust proxy", true);

process.on('exit', () => {
    Object.values(hosted).forEach(item => item.process.kill());
});

const server = app.listen(5000, () => log("port", "http://localhost:5000"));
server.timeout = 0;
server.requestTimeout = 0;
server.headersTimeout = 0;
server.keepAliveTimeout = 0;

const wss = new WebSocketServer({server});

const clients = new Map();

wss.on('connection', (ws) => {
    const clientId = Math.random().toString(36).substring(2, 9);
    clients.set(clientId, ws);
    
    // Tell the client its own ID
    ws.send(JSON.stringify({ type: 'init', id: clientId }));

    ws.on('message', (message) => {
        const data = JSON.parse(message);
        
        // Forward message to the specific target peer
        if (data.target && clients.has(data.target)) {
            clients.get(data.target).send(JSON.stringify({
                ...data,
                sender: clientId
            }));
        }
    });

    ws.on('close', () => {
        clients.delete(clientId);
    });
});

fixFolderStructure();
ReloadAppdata();

module.exports = { log, error };