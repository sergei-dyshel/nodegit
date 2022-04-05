var buildFlags = require("../utils/buildFlags");
var spawn = require("child_process").spawn;
var path = require("path");

function runNodePreGyp(...args) {
  var nodePreGyp = "node-pre-gyp";

  if (process.platform === "win32") {
    nodePreGyp += ".cmd";
  }

  return new Promise(function(resolve, reject) {
    var spawnedNodePreGyp = spawn(nodePreGyp, args, {
      env: Object.assign({}, process.env, {
        npm_config_node_gyp: path.join(__dirname, "..", "node_modules",
          "node-gyp", "bin", "node-gyp.js")
      })
    });

    spawnedNodePreGyp.stdout.on("data", function(data) {
      console.info(data.toString().trim());
    });

    spawnedNodePreGyp.stderr.on("data", function(data) {
      console.error(data.toString().trim());
    });

    spawnedNodePreGyp.on("close", function(code) {
      if (!code) {
        resolve();
      } else {
        reject(code);
      }
    });
  })
}

module.exports = async function install() {
  console.log("[nodegit] Running install script");

  await runNodePreGyp("configure");
  await runNodePreGyp("build", "--", "acquireOpenSSL", "configureLibssh2")
  await runNodePreGyp("build", "--", "nodegit")

  console.info("[nodegit] Completed installation successfully.");
};

// Called on the command line
if (require.main === module) {
  module.exports()
    .catch(function(e) {
      console.error("[nodegit] ERROR - Could not finish install");
      console.error("[nodegit] ERROR - finished with error code: " + e);
      process.exit(e);
    });
}
