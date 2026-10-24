package com.arcade.engine;

import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.Reader;
import java.net.HttpURLConnection;
import java.net.URI;
import java.net.URL;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.CancellationException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * NetworkRomSource manages discovery, catalog parsing, and streaming downloads
 * of arcade ROMs from local IPv4 servers (e.g. Python http.server, Nginx, Apache)
 * and dedicated HTTPS servers, as well as direct ZIP download URLs.
 */
public class NetworkRomSource {

    private static final String TAG = "NetworkRomSource";
    private static final int CONNECT_TIMEOUT_MS = 15000;
    private static final int READ_TIMEOUT_MS = 30000;
    private static final int MAX_REDIRECTS = 5;
    private static final int BUFFER_SIZE = 32768;

    // Pattern to extract <a ... href="..." ...>(optional text)</a>
    private static final Pattern HTML_LINK_PATTERN = Pattern.compile(
            "<a\\s+[^>]*href\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))[^>]*>(?:([\\s\\S]*?)<\\/a>)?",
            Pattern.CASE_INSENSITIVE
    );

    // Pattern to match IPv4 addresses (e.g. 192.168.1.1, 10.0.0.5, 127.0.0.1)
    private static final Pattern IPV4_PATTERN = Pattern.compile(
            "^(?:[0-9]{1,3}\\.){3}[0-9]{1,3}(?::[0-9]+)?(?:/.*)?$"
    );

    /**
     * Represents a ROM item discovered from a network source.
     */
    public static class NetworkRomItem {
        private final String name;
        private final String fileName;
        private final String url;
        private final long size;
        private final boolean isDirectZip;

        public NetworkRomItem(String name, String fileName, String url, long size, boolean isDirectZip) {
            this.name = name;
            this.fileName = fileName;
            this.url = url;
            this.size = size;
            this.isDirectZip = isDirectZip;
        }

        public String getName() {
            return name;
        }

        public String getFileName() {
            return fileName;
        }

        public String getUrl() {
            return url;
        }

        public long getSize() {
            return size;
        }

        public boolean isDirectZip() {
            return isDirectZip;
        }

        @Override
        public String toString() {
            return "NetworkRomItem{" +
                    "name='" + name + '\'' +
                    ", fileName='" + fileName + '\'' +
                    ", url='" + url + '\'' +
                    ", size=" + size +
                    '}';
        }
    }

    /**
     * Callback for download progress and completion.
     */
    public interface DownloadProgressListener {
        void onProgress(long bytesRead, long totalBytes, double speedBytesPerSec);
        void onComplete(File file);
        void onError(Exception error);
    }

    /**
     * Normalizes a user-entered URL.
     * Supports:
     *  - Local IPv4 servers (e.g. 192.168.1.100:8000 -> http://192.168.1.100:8000/)
     *  - Raw hostnames without scheme -> http://
     *  - Dedicated HTTPS/HTTP servers
     *  - Rejects forbidden schemes (file://, content://, javascript://, data://, etc.)
     */
    public static String normalizeUrl(String input) {
        if (input == null || input.trim().isEmpty()) {
            throw new IllegalArgumentException("URL cannot be empty");
        }

        String trimmed = input.trim();

        // Check for forbidden or unsupported schemes
        int colonIdx = trimmed.indexOf(':');
        if (colonIdx > 0) {
            String candidateScheme = trimmed.substring(0, colonIdx).toLowerCase(Locale.ROOT);
            // RFC 3986 scheme: starts with letter followed by letters, digits, +, -, .
            if (candidateScheme.matches("^[a-z][a-z0-9+.-]*$")) {
                if (!candidateScheme.equals("http") && !candidateScheme.equals("https")) {
                    throw new SecurityException("Forbidden or unsupported URI scheme: " + candidateScheme);
                }
            }
        }

        String normalized;
        if (trimmed.startsWith("//")) {
            normalized = "http:" + trimmed;
        } else if (trimmed.regionMatches(true, 0, "http://", 0, 7) ||
                   trimmed.regionMatches(true, 0, "https://", 0, 8)) {
            normalized = trimmed;
        } else {
            // Default raw IPs and unadorned hostnames to http://
            normalized = "http://" + trimmed;
        }

        // Validate syntax with java.net.URI
        try {
            URI uri = URI.create(normalized);
            String scheme = uri.getScheme();
            if (scheme == null || (!scheme.equalsIgnoreCase("http") && !scheme.equalsIgnoreCase("https"))) {
                throw new SecurityException("Only HTTP and HTTPS protocols are permitted: " + normalized);
            }
            if (uri.getHost() == null || uri.getHost().trim().isEmpty()) {
                throw new IllegalArgumentException("Invalid host in URL: " + normalized);
            }

            // Ensure path is at least "/" if not ending in file
            String path = uri.getPath();
            if ((path == null || path.isEmpty()) && !normalized.contains("?")) {
                normalized += "/";
            }
            return normalized;
        } catch (Exception e) {
            if (e instanceof SecurityException) throw (SecurityException) e;
            throw new IllegalArgumentException("Invalid URL format: " + input, e);
        }
    }

    /**
     * Determines whether a given URL directly points to a .zip archive.
     */
    public static boolean isDirectZipUrl(String url) {
        if (url == null || url.trim().isEmpty()) return false;
        try {
            URI uri = URI.create(normalizeUrl(url));
            String path = uri.getPath();
            if (path == null) return false;
            return path.toLowerCase(Locale.ROOT).endsWith(".zip");
        } catch (Exception e) {
            return false;
        }
    }

    /**
     * Sanitizes a server-provided filename to prevent path traversal and arbitrary filesystem overwrite.
     */
    public static String sanitizeFileName(String rawName) {
        if (rawName == null || rawName.trim().isEmpty()) {
            return "rom.zip";
        }

        String decoded;
        try {
            decoded = URLDecoder.decode(rawName.trim(), StandardCharsets.UTF_8.name());
        } catch (Exception e) {
            decoded = rawName.trim();
        }

        // Standardize slashes and take basename
        decoded = decoded.replace('\\', '/');
        int lastSlash = decoded.lastIndexOf('/');
        if (lastSlash != -1) {
            decoded = decoded.substring(lastSlash + 1);
        }

        // Strip URL query strings or fragments if attached after .zip
        int qIdx = decoded.indexOf('?');
        if (qIdx != -1) {
            if (decoded.substring(0, qIdx).toLowerCase(Locale.ROOT).endsWith(".zip")) {
                decoded = decoded.substring(0, qIdx);
            } else {
                decoded = decoded.replace('?', '_');
            }
        }
        int hashIdx = decoded.indexOf('#');
        if (hashIdx != -1) {
            if (decoded.substring(0, hashIdx).toLowerCase(Locale.ROOT).endsWith(".zip")) {
                decoded = decoded.substring(0, hashIdx);
            } else {
                decoded = decoded.replace('#', '_');
            }
        }

        // Strip control characters, quotes, and dangerous characters
        String sanitized = decoded.replaceAll("[^a-zA-Z0-9._-]", "_");

        // Prevent traversal tokens like ".." or "."
        while (sanitized.contains("..")) {
            sanitized = sanitized.replace("..", "_");
        }
        sanitized = sanitized.trim();
        if (sanitized.isEmpty() || sanitized.equals(".") || sanitized.equals("..")) {
            sanitized = "rom_" + System.currentTimeMillis() + ".zip";
        }

        if (!sanitized.toLowerCase(Locale.ROOT).endsWith(".zip")) {
            sanitized += ".zip";
        }

        return sanitized;
    }

    /**
     * Resolves a target destination file within destDir with strict path traversal checks.
     */
    public static File resolveDestinationFile(File destDir, String fileName) throws IOException {
        if (destDir == null) {
            throw new IllegalArgumentException("Destination directory cannot be null");
        }
        if (!destDir.exists()) {
            destDir.mkdirs();
        }

        String safeName = sanitizeFileName(fileName);
        File target = new File(destDir, safeName);

        String canonicalDest = destDir.getCanonicalPath();
        String canonicalTarget = target.getCanonicalPath();

        if (!canonicalTarget.startsWith(canonicalDest + File.separator) && !canonicalTarget.equals(canonicalDest)) {
            throw new SecurityException("Path traversal attempt detected for file: " + fileName);
        }

        return target;
    }

    /**
     * Parses standard HTML directory listings (Apache, Nginx, Python http.server, Caddy, Lighttpd).
     * Extracts .zip links, decodes URL percent-encoding, and resolves relative URLs against baseUrl.
     */
    public static List<NetworkRomItem> parseHtmlListing(String htmlContent, String baseUrl) {
        if (htmlContent == null || htmlContent.trim().isEmpty()) {
            return Collections.emptyList();
        }

        URI baseUri = URI.create(baseUrl);
        List<NetworkRomItem> items = new ArrayList<>();
        Set<String> seenUrls = new HashSet<>();

        Matcher matcher = HTML_LINK_PATTERN.matcher(htmlContent);
        while (matcher.find()) {
            String href = matcher.group(1);
            if (href == null) href = matcher.group(2);
            if (href == null) href = matcher.group(3);

            if (href == null || href.trim().isEmpty()) continue;
            href = href.trim();

            // Ignore mailto, javascript, hashes
            if (href.startsWith("#") || href.toLowerCase(Locale.ROOT).startsWith("javascript:")) {
                continue;
            }

            // Extract path without query parameters or anchors
            String pathOnly = href;
            int qIdx = pathOnly.indexOf('?');
            if (qIdx != -1) pathOnly = pathOnly.substring(0, qIdx);
            int hashIdx = pathOnly.indexOf('#');
            if (hashIdx != -1) pathOnly = pathOnly.substring(0, hashIdx);

            if (!pathOnly.toLowerCase(Locale.ROOT).endsWith(".zip")) {
                continue;
            }

            try {
                URI resolvedUri = baseUri.resolve(href);
                String fullUrl = resolvedUri.toString();

                if (seenUrls.contains(fullUrl)) continue;
                seenUrls.add(fullUrl);

                // Derive clean filename from path
                String rawName = pathOnly;
                int slash = rawName.lastIndexOf('/');
                if (slash != -1) rawName = rawName.substring(slash + 1);

                String cleanFileName = sanitizeFileName(rawName);

                // Display name: use link text if clean, or clean filename
                String linkText = matcher.group(4);
                String displayName = cleanFileName;
                if (linkText != null) {
                    // Strip nested tags from link text
                    String strippedText = linkText.replaceAll("<[^>]*>", "").trim();
                    if (!strippedText.isEmpty()) {
                        displayName = strippedText;
                    }
                }

                items.add(new NetworkRomItem(displayName, cleanFileName, fullUrl, -1, false));
            } catch (Exception ignored) {
                // Ignore malformed hrefs
            }
        }

        return items;
    }

    /**
     * Parses JSON catalogs supporting:
     *  - Array of strings: ["mslug.zip", "kof97.zip"]
     *  - Array of objects: [{"name": "mslug.zip", "url": "...", "size": 12345}, ...]
     *  - Object with files or roms array: {"files": [...]} or {"roms": [...]}
     */
    public static List<NetworkRomItem> parseJsonCatalog(String jsonContent, String baseUrl) {
        if (jsonContent == null || jsonContent.trim().isEmpty()) {
            return Collections.emptyList();
        }

        URI baseUri = URI.create(baseUrl);
        List<NetworkRomItem> items = new ArrayList<>();
        Set<String> seenUrls = new HashSet<>();

        try {
            String trimmed = jsonContent.trim();
            JSONArray array = null;

            if (trimmed.startsWith("[")) {
                array = new JSONArray(trimmed);
            } else if (trimmed.startsWith("{")) {
                JSONObject obj = new JSONObject(trimmed);
                String[] candidateKeys = {"files", "roms", "items", "games", "catalog", "list"};
                for (String key : candidateKeys) {
                    if (obj.has(key) && !obj.isNull(key)) {
                        array = obj.optJSONArray(key);
                        if (array != null) break;
                    }
                }
            }

            if (array == null) {
                return Collections.emptyList();
            }

            for (int i = 0; i < array.length(); i++) {
                Object element = array.get(i);
                if (element instanceof String) {
                    String str = (String) element;
                    if (str.toLowerCase(Locale.ROOT).endsWith(".zip")) {
                        URI resolved = baseUri.resolve(str);
                        String fullUrl = resolved.toString();
                        if (!seenUrls.contains(fullUrl)) {
                            seenUrls.add(fullUrl);
                            String fileName = sanitizeFileName(str);
                            items.add(new NetworkRomItem(fileName, fileName, fullUrl, -1, false));
                        }
                    }
                } else if (element instanceof JSONObject) {
                    JSONObject itemObj = (JSONObject) element;

                    // Extract title / display name
                    String displayName = null;
                    String[] titleKeys = {"title", "name", "description"};
                    for (String k : titleKeys) {
                        if (itemObj.has(k)) {
                            displayName = itemObj.optString(k, null);
                            if (displayName != null && !displayName.trim().isEmpty()) break;
                        }
                    }

                    // Extract filename explicitly if provided
                    String explicitFileName = null;
                    String[] fileKeys = {"filename", "file", "rom"};
                    for (String k : fileKeys) {
                        if (itemObj.has(k)) {
                            explicitFileName = itemObj.optString(k, null);
                            if (explicitFileName != null && !explicitFileName.trim().isEmpty()) break;
                        }
                    }

                    // Extract URL
                    String url = null;
                    String[] urlKeys = {"url", "download_url", "path", "link", "href"};
                    for (String k : urlKeys) {
                        if (itemObj.has(k)) {
                            url = itemObj.optString(k, null);
                            if (url != null && !url.trim().isEmpty()) break;
                        }
                    }

                    if (url == null && explicitFileName != null) {
                        url = explicitFileName;
                    }
                    if (url == null && displayName != null && displayName.toLowerCase(Locale.ROOT).endsWith(".zip")) {
                        url = displayName;
                    }

                    if (url != null) {
                        URI resolved = baseUri.resolve(url);
                        String fullUrl = resolved.toString();
                        if (!seenUrls.contains(fullUrl)) {
                            seenUrls.add(fullUrl);

                            long size = -1;
                            String[] sizeKeys = {"size", "bytes", "file_size", "length"};
                            for (String sk : sizeKeys) {
                                if (itemObj.has(sk)) {
                                    size = itemObj.optLong(sk, -1);
                                    if (size >= 0) break;
                                }
                            }

                            // Determine raw filename: explicit filename, or extracted from url path, or fallback to name
                            String rawFile = explicitFileName;
                            if (rawFile == null) {
                                String path = resolved.getPath();
                                if (path != null) {
                                    int sl = path.lastIndexOf('/');
                                    rawFile = sl != -1 ? path.substring(sl + 1) : path;
                                }
                            }
                            if (rawFile == null || rawFile.isEmpty() || !rawFile.toLowerCase(Locale.ROOT).endsWith(".zip")) {
                                if (displayName != null && displayName.toLowerCase(Locale.ROOT).endsWith(".zip")) {
                                    rawFile = displayName;
                                } else if (displayName != null) {
                                    rawFile = displayName + ".zip";
                                } else {
                                    rawFile = "rom.zip";
                                }
                            }

                            String cleanFileName = sanitizeFileName(rawFile);
                            String finalDisplayName = (displayName != null && !displayName.trim().isEmpty()) ? displayName : cleanFileName;
                            items.add(new NetworkRomItem(finalDisplayName, cleanFileName, fullUrl, size, false));
                        }
                    }
                }
            }
        } catch (Exception e) {
            // Malformed or incompatible JSON; will fallback to HTML if applicable
        }

        return items;
    }

    /**
     * Auto-detects whether the catalog content is JSON or HTML and parses accordingly.
     */
    public static List<NetworkRomItem> parseCatalog(String content, String contentType, String baseUrl) {
        if (content == null || content.trim().isEmpty()) {
            return Collections.emptyList();
        }

        boolean isJson = (contentType != null && contentType.toLowerCase(Locale.ROOT).contains("json")) ||
                content.trim().startsWith("[") || content.trim().startsWith("{");

        if (isJson) {
            List<NetworkRomItem> items = parseJsonCatalog(content, baseUrl);
            if (!items.isEmpty()) return items;
        }

        return parseHtmlListing(content, baseUrl);
    }

    /**
     * Synchronously fetches catalog from a server URL.
     * If the URL directly points to a .zip file, returns a single NetworkRomItem.
     */
    public static List<NetworkRomItem> fetchCatalogSync(String serverUrl, AtomicBoolean cancelSignal) throws IOException {
        String normalizedUrl = normalizeUrl(serverUrl);

        if (isDirectZipUrl(normalizedUrl)) {
            URI uri = URI.create(normalizedUrl);
            String path = uri.getPath();
            String rawName = path;
            int lastSlash = rawName.lastIndexOf('/');
            if (lastSlash != -1) rawName = rawName.substring(lastSlash + 1);
            String safeName = sanitizeFileName(rawName);
            return Collections.singletonList(new NetworkRomItem(safeName, safeName, normalizedUrl, -1, true));
        }

        HttpURLConnection conn = openConnectionWithRedirects(normalizedUrl, cancelSignal);
        try {
            int code = conn.getResponseCode();
            if (code < 200 || code >= 300) {
                throw new IOException("Server returned HTTP error " + code + ": " + conn.getResponseMessage());
            }

            String contentType = conn.getContentType();
            String responseBody = readStringFromStream(conn.getInputStream(), cancelSignal);

            return parseCatalog(responseBody, contentType, normalizedUrl);
        } finally {
            conn.disconnect();
        }
    }

    /**
     * Synchronously downloads a ROM file with progress reporting and cancellation support.
     * Uses atomic temporary file write to ensure ROMs are never partially written or corrupt.
     */
    public static File downloadRomSync(String romUrl, File destDir, String suggestedFileName,
                                       AtomicBoolean cancelSignal, DownloadProgressListener listener) throws IOException {
        if (cancelSignal != null && cancelSignal.get()) {
            throw new CancellationException("Download cancelled before starting");
        }

        String normalizedUrl = normalizeUrl(romUrl);

        String targetFileName = suggestedFileName;
        if (targetFileName == null || targetFileName.trim().isEmpty()) {
            URI uri = URI.create(normalizedUrl);
            String path = uri.getPath();
            if (path != null) {
                int lastSlash = path.lastIndexOf('/');
                if (lastSlash != -1) targetFileName = path.substring(lastSlash + 1);
            }
        }
        targetFileName = sanitizeFileName(targetFileName);

        File destFile = resolveDestinationFile(destDir, targetFileName);
        File tempFile = new File(destDir, ".tmp_" + System.currentTimeMillis() + "_" + destFile.getName());

        HttpURLConnection conn = openConnectionWithRedirects(normalizedUrl, cancelSignal);
        InputStream in = null;
        OutputStream out = null;

        try {
            int responseCode = conn.getResponseCode();
            if (responseCode < 200 || responseCode >= 300) {
                throw new IOException("Server returned HTTP " + responseCode + ": " + conn.getResponseMessage());
            }

            long totalBytes = conn.getContentLengthLong();
            in = new BufferedInputStream(conn.getInputStream(), BUFFER_SIZE);
            out = new FileOutputStream(tempFile);

            byte[] buffer = new byte[BUFFER_SIZE];
            long bytesRead = 0;
            int read;
            long startTime = System.currentTimeMillis();
            long lastProgressUpdate = startTime;

            while ((read = in.read(buffer)) != -1) {
                if (cancelSignal != null && cancelSignal.get()) {
                    throw new CancellationException("Download cancelled by user");
                }

                out.write(buffer, 0, read);
                bytesRead += read;

                long now = System.currentTimeMillis();
                if (listener != null && (now - lastProgressUpdate >= 100 || bytesRead == totalBytes)) {
                    double elapsedSec = Math.max(0.001, (now - startTime) / 1000.0);
                    double speed = bytesRead / elapsedSec;
                    listener.onProgress(bytesRead, totalBytes, speed);
                    lastProgressUpdate = now;
                }
            }

            out.flush();
            out.close();
            out = null;

            // Atomically replace target file
            try {
                Files.move(tempFile.toPath(), destFile.toPath(),
                        StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            } catch (Exception e) {
                // Fallback to copy and delete if filesystem does not support atomic move
                Files.copy(tempFile.toPath(), destFile.toPath(), StandardCopyOption.REPLACE_EXISTING);
                tempFile.delete();
            }

            if (listener != null) {
                listener.onComplete(destFile);
            }

            return destFile;
        } catch (Exception e) {
            if (tempFile.exists()) {
                tempFile.delete();
            }
            if (listener != null) {
                listener.onError(e);
            }
            if (e instanceof IOException) throw (IOException) e;
            throw new IOException("Download failed: " + e.getMessage(), e);
        } finally {
            if (out != null) {
                try { out.close(); } catch (Exception ignored) {}
            }
            if (in != null) {
                try { in.close(); } catch (Exception ignored) {}
            }
            conn.disconnect();
        }
    }

    /**
     * Follows HTTP 3xx redirects safely up to MAX_REDIRECTS, preventing infinite loops or scheme escapes.
     */
    private static HttpURLConnection openConnectionWithRedirects(String initialUrl, AtomicBoolean cancelSignal) throws IOException {
        String currentUrl = initialUrl;
        int redirectCount = 0;

        while (redirectCount <= MAX_REDIRECTS) {
            if (cancelSignal != null && cancelSignal.get()) {
                throw new CancellationException("Operation cancelled");
            }

            URL url = new URL(currentUrl);
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(CONNECT_TIMEOUT_MS);
            conn.setReadTimeout(READ_TIMEOUT_MS);
            conn.setInstanceFollowRedirects(false);
            conn.setRequestProperty("User-Agent", "ArcadeEngine/1.0 (Android)");
            conn.setRequestProperty("Accept", "text/html,application/json,*/*");

            int code = conn.getResponseCode();
            if (code == HttpURLConnection.HTTP_MOVED_PERM ||
                code == HttpURLConnection.HTTP_MOVED_TEMP ||
                code == HttpURLConnection.HTTP_SEE_OTHER ||
                code == 307 || code == 308) {

                String location = conn.getHeaderField("Location");
                conn.disconnect();

                if (location == null || location.trim().isEmpty()) {
                    throw new IOException("HTTP redirect without Location header from: " + currentUrl);
                }

                URI baseUri = URI.create(currentUrl);
                URI targetUri = baseUri.resolve(location.trim());
                currentUrl = normalizeUrl(targetUri.toString());
                redirectCount++;
            } else {
                return conn;
            }
        }

        throw new IOException("Too many HTTP redirects (exceeded " + MAX_REDIRECTS + ")");
    }

    /**
     * Reads UTF-8 response content from an InputStream with cancellation check.
     */
    private static String readStringFromStream(InputStream stream, AtomicBoolean cancelSignal) throws IOException {
        StringBuilder sb = new StringBuilder();
        try (Reader reader = new InputStreamReader(new BufferedInputStream(stream), StandardCharsets.UTF_8)) {
            char[] buffer = new char[8192];
            int read;
            while ((read = reader.read(buffer)) != -1) {
                if (cancelSignal != null && cancelSignal.get()) {
                    throw new CancellationException("Operation cancelled while reading response");
                }
                sb.append(buffer, 0, read);
            }
        }
        return sb.toString();
    }
}
