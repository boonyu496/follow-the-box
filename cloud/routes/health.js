function createHealthRoutes(options) {
  const {
    healthSummary,
    operatorToken,
    send,
  } = options;

  function isLocalAddress(remoteIp) {
    return /^(127\.|::1$|::ffff:127\.|::ffff:10\.|10\.|172\.(1[6-9]|2\d|3[01])\.|192\.168\.|::ffff:192\.168\.)/.test(remoteIp);
  }

  function handle(req, res, url) {
    if (url.pathname === "/api/health" && req.method === "GET") {
      send(res, 200, healthSummary());
      return true;
    }

    // Local config exposes operator token only to RFC-1918 / loopback clients.
    if (url.pathname === "/api/config" && req.method === "GET") {
      const remoteIp = req.socket?.remoteAddress || "";
      const isLocal = isLocalAddress(remoteIp);
      send(res, 200, {
        ok: true,
        local: isLocal,
        operator_token: isLocal ? operatorToken : "",
      });
      return true;
    }

    return false;
  }

  return {
    handle,
  };
}

module.exports = {
  createHealthRoutes,
};
