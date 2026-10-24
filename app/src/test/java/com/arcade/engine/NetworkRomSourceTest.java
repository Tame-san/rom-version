package com.arcade.engine;

import org.junit.Assert;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.IOException;
import java.util.List;

public class NetworkRomSourceTest {

    @Rule
    public TemporaryFolder tempFolder = new TemporaryFolder();

    // ---------------------------------------------------------
    // 1. URL Normalization Tests
    // ---------------------------------------------------------

    @Test
    public void testNormalizeUrl_LocalIpv4WithoutScheme() {
        String normalized = NetworkRomSource.normalizeUrl("192.168.1.100:8000");
        Assert.assertEquals("http://192.168.1.100:8000/", normalized);

        String normalizedWithSlash = NetworkRomSource.normalizeUrl("192.168.1.50:8080/");
        Assert.assertEquals("http://192.168.1.50:8080/", normalizedWithSlash);

        String normalizedPath = NetworkRomSource.normalizeUrl("10.0.0.5/roms/");
        Assert.assertEquals("http://10.0.0.5/roms/", normalizedPath);
    }

    @Test
    public void testNormalizeUrl_LocalIpv4WithHttp() {
        String normalized = NetworkRomSource.normalizeUrl("http://192.168.1.100:8000/");
        Assert.assertEquals("http://192.168.1.100:8000/", normalized);
    }

    @Test
    public void testNormalizeUrl_DedicatedHttps() {
        String normalized = NetworkRomSource.normalizeUrl("https://roms.arcade.org/catalog/");
        Assert.assertEquals("https://roms.arcade.org/catalog/", normalized);

        String normalizedNas = NetworkRomSource.normalizeUrl("https://my-nas.lan:8443/");
        Assert.assertEquals("https://my-nas.lan:8443/", normalizedNas);
    }

    @Test
    public void testNormalizeUrl_DomainsWithoutScheme() {
        String normalized = NetworkRomSource.normalizeUrl("roms.arcade.org/catalog/");
        Assert.assertEquals("http://roms.arcade.org/catalog/", normalized);
    }

    // ---------------------------------------------------------
    // 2. Scheme Security Rejection Tests
    // ---------------------------------------------------------

    @Test(expected = SecurityException.class)
    public void testRejectForbiddenScheme_File() {
        NetworkRomSource.normalizeUrl("file:///etc/passwd");
    }

    @Test(expected = SecurityException.class)
    public void testRejectForbiddenScheme_Content() {
        NetworkRomSource.normalizeUrl("content://media/external/file/123");
    }

    @Test(expected = SecurityException.class)
    public void testRejectForbiddenScheme_Javascript() {
        NetworkRomSource.normalizeUrl("javascript:alert(1)");
    }

    @Test(expected = SecurityException.class)
    public void testRejectForbiddenScheme_Data() {
        NetworkRomSource.normalizeUrl("data:text/html,<html></html>");
    }

    @Test(expected = SecurityException.class)
    public void testRejectForbiddenScheme_Ftp() {
        NetworkRomSource.normalizeUrl("ftp://ftp.arcade.org/roms/");
    }

    @Test(expected = IllegalArgumentException.class)
    public void testRejectEmptyUrl() {
        NetworkRomSource.normalizeUrl("   ");
    }

    // ---------------------------------------------------------
    // 3. Direct ZIP Detection
    // ---------------------------------------------------------

    @Test
    public void testIsDirectZipUrl() {
        Assert.assertTrue(NetworkRomSource.isDirectZipUrl("http://192.168.1.50/roms/mslug.zip"));
        Assert.assertTrue(NetworkRomSource.isDirectZipUrl("https://roms.arcade.org/kof97.ZIP"));
        Assert.assertTrue(NetworkRomSource.isDirectZipUrl("192.168.1.100:8000/neogeo.zip"));

        Assert.assertFalse(NetworkRomSource.isDirectZipUrl("http://192.168.1.50/roms/"));
        Assert.assertFalse(NetworkRomSource.isDirectZipUrl("https://roms.arcade.org/catalog"));
    }

    // ---------------------------------------------------------
    // 4. HTML Directory Listing Parsing Tests
    // ---------------------------------------------------------

    @Test
    public void testParseHtmlListing_PythonHttpServer() {
        String html = "<!DOCTYPE HTML>\n" +
                "<html>\n" +
                "<head><title>Directory listing for /</title></head>\n" +
                "<body>\n" +
                "<h1>Directory listing for /</h1>\n" +
                "<hr>\n" +
                "<ul>\n" +
                "<li><a href=\"mslug.zip\">mslug.zip</a></li>\n" +
                "<li><a href=\"kof98.zip\">kof98.zip</a></li>\n" +
                "<li><a href=\"subdir/\">subdir/</a></li>\n" +
                "<li><a href=\"readme.txt\">readme.txt</a></li>\n" +
                "</ul>\n" +
                "<hr>\n" +
                "</body>\n" +
                "</html>";

        String base = "http://192.168.1.100:8000/";
        List<NetworkRomSource.NetworkRomItem> items = NetworkRomSource.parseHtmlListing(html, base);

        Assert.assertEquals(2, items.size());
        Assert.assertEquals("mslug.zip", items.get(0).getFileName());
        Assert.assertEquals("http://192.168.1.100:8000/mslug.zip", items.get(0).getUrl());
        Assert.assertEquals("kof98.zip", items.get(1).getFileName());
        Assert.assertEquals("http://192.168.1.100:8000/kof98.zip", items.get(1).getUrl());
    }

    @Test
    public void testParseHtmlListing_NginxListingWithRelativeUrlsAndParams() {
        String html = "<html><head><title>Index of /roms/</title></head><body>\n" +
                "<h1>Index of /roms/</h1><hr><pre>\n" +
                "<a href=\"../\">../</a>\n" +
                "<a href=\"garou.zip?dl=1#view\">garou.zip?dl=1#view</a> 24-Oct-2026 10:00 83886080\n" +
                "<a href=\"/downloads/sf2.zip\">sf2.zip</a> 24-Oct-2026 10:05 123456\n" +
                "</pre><hr></body></html>";

        String base = "http://192.168.1.50/roms/";
        List<NetworkRomSource.NetworkRomItem> items = NetworkRomSource.parseHtmlListing(html, base);

        Assert.assertEquals(2, items.size());
        Assert.assertEquals("garou.zip", items.get(0).getFileName());
        Assert.assertEquals("http://192.168.1.50/roms/garou.zip?dl=1#view", items.get(0).getUrl());
        Assert.assertEquals("sf2.zip", items.get(1).getFileName());
        Assert.assertEquals("http://192.168.1.50/downloads/sf2.zip", items.get(1).getUrl());
    }

    @Test
    public void testParseHtmlListing_UrlEncodedSpaces() {
        String html = "<div><a href=\"Street%20Fighter%20II.zip\"><span>Street Fighter II</span></a></div>";
        String base = "https://roms.arcade.org/";
        List<NetworkRomSource.NetworkRomItem> items = NetworkRomSource.parseHtmlListing(html, base);

        Assert.assertEquals(1, items.size());
        Assert.assertEquals("Street_Fighter_II.zip", items.get(0).getFileName());
        Assert.assertEquals("Street Fighter II", items.get(0).getName());
        Assert.assertEquals("https://roms.arcade.org/Street%20Fighter%20II.zip", items.get(0).getUrl());
    }

    // ---------------------------------------------------------
    // 5. JSON Catalog Parsing Tests
    // ---------------------------------------------------------

    @Test
    public void testParseJsonCatalog_StringArray() {
        String json = "[\"mslug.zip\", \"kof97.zip\", \"invalid.txt\"]";
        String base = "http://192.168.1.100:8000/catalog/";
        List<NetworkRomSource.NetworkRomItem> items = NetworkRomSource.parseJsonCatalog(json, base);

        Assert.assertEquals(2, items.size());
        Assert.assertEquals("mslug.zip", items.get(0).getFileName());
        Assert.assertEquals("http://192.168.1.100:8000/catalog/mslug.zip", items.get(0).getUrl());
        Assert.assertEquals("kof97.zip", items.get(1).getFileName());
    }

    @Test
    public void testParseJsonCatalog_ObjectArray() {
        String json = "[\n" +
                "  {\"name\": \"Metal Slug 3\", \"url\": \"mslug3.zip\", \"size\": 82944000},\n" +
                "  {\"name\": \"Garou: MotW\", \"url\": \"https://external.cdn/garou.zip\", \"size\": 75000000}\n" +
                "]";
        String base = "http://10.0.0.5/";
        List<NetworkRomSource.NetworkRomItem> items = NetworkRomSource.parseJsonCatalog(json, base);

        Assert.assertEquals(2, items.size());
        Assert.assertEquals("Metal Slug 3", items.get(0).getName());
        Assert.assertEquals("mslug3.zip", items.get(0).getFileName());
        Assert.assertEquals("http://10.0.0.5/mslug3.zip", items.get(0).getUrl());
        Assert.assertEquals(82944000L, items.get(0).getSize());

        Assert.assertEquals("Garou: MotW", items.get(1).getName());
        Assert.assertEquals("https://external.cdn/garou.zip", items.get(1).getUrl());
        Assert.assertEquals(75000000L, items.get(1).getSize());
    }

    @Test
    public void testParseJsonCatalog_NestedFilesOrRomsObject() {
        String jsonWithFiles = "{\"files\": [{\"name\": \"neogeo.zip\", \"size\": 1500000}]}";
        String base = "http://192.168.1.200/";
        List<NetworkRomSource.NetworkRomItem> items1 = NetworkRomSource.parseJsonCatalog(jsonWithFiles, base);
        Assert.assertEquals(1, items1.size());
        Assert.assertEquals("neogeo.zip", items1.get(0).getFileName());

        String jsonWithRoms = "{\"roms\": [\"mslugx.zip\", \"samsho.zip\"]}";
        List<NetworkRomSource.NetworkRomItem> items2 = NetworkRomSource.parseJsonCatalog(jsonWithRoms, base);
        Assert.assertEquals(2, items2.size());
        Assert.assertEquals("mslugx.zip", items2.get(0).getFileName());
    }

    // ---------------------------------------------------------
    // 6. Path Traversal & Filename Sanitization Tests
    // ---------------------------------------------------------

    @Test
    public void testSanitizeFileName_StripsDirectoryTraversal() {
        Assert.assertEquals("mslug.zip", NetworkRomSource.sanitizeFileName("mslug.zip"));
        Assert.assertEquals("mslug.zip", NetworkRomSource.sanitizeFileName("../../mslug.zip"));
        Assert.assertEquals("mslug.zip", NetworkRomSource.sanitizeFileName("..\\..\\mslug.zip"));
        Assert.assertEquals("neogeo.zip", NetworkRomSource.sanitizeFileName("/etc/../neogeo.zip"));
        Assert.assertEquals("game_123.zip", NetworkRomSource.sanitizeFileName("game?123.zip#hash"));
    }

    @Test
    public void testResolveDestinationFile_EnsuresStrictSandbox() throws IOException {
        File sandbox = tempFolder.newFolder("roms");

        File normal = NetworkRomSource.resolveDestinationFile(sandbox, "mslug.zip");
        Assert.assertTrue(normal.getCanonicalPath().startsWith(sandbox.getCanonicalPath()));
        Assert.assertEquals(new File(sandbox, "mslug.zip"), normal);

        // Even with path traversal input, sanitization forces safe path inside sandbox
        File traversal = NetworkRomSource.resolveDestinationFile(sandbox, "../../../etc/passwd.zip");
        Assert.assertTrue(traversal.getCanonicalPath().startsWith(sandbox.getCanonicalPath()));
        Assert.assertEquals("passwd.zip", traversal.getName());
    }
}
