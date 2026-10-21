package com.arcade.engine;

import android.content.Context;
import android.util.Log;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

public class RomNormalizer {

    private static final String TAG = "RomNormalizer";
    private static final Pattern NEO_ROM_PATTERN = Pattern.compile("^([0-9]{3})\\.([a-zA-Z0-9]+)$");

    /**
     * Inspects the given ROM zip and returns a version guaranteed to have FBNeo-compatible
     * filenames. If already normalized, the source file is returned directly.
     */
    public static File getNormalizedRomFile(Context context, File sourceFile) {
        if (sourceFile == null || !sourceFile.exists() || !sourceFile.getName().toLowerCase().endsWith(".zip")) {
            return sourceFile;
        }

        try {
            if (!needsNormalization(sourceFile)) {
                Log.i(TAG, sourceFile.getName() + " is already 100% FBNeo-compliant.");
                return sourceFile;
            }

            File targetDir = new File(context.getExternalFilesDir(null), "roms");
            if (!targetDir.exists()) {
                targetDir.mkdirs();
            }

            String baseName = sourceFile.getName();
            File cachedNormalized = new File(targetDir, "fbneo_" + baseName);

            // If cache exists and is newer than source, use cached version
            if (cachedNormalized.exists() && cachedNormalized.length() > 0 &&
                cachedNormalized.lastModified() >= sourceFile.lastModified()) {
                Log.i(TAG, "Using cached normalized ROM: " + cachedNormalized.getAbsolutePath());
                return cachedNormalized;
            }

            Log.i(TAG, "Normalizing " + baseName + " for FBNeo engine...");
            File tempFile = new File(targetDir, "temp_" + System.currentTimeMillis() + "_" + baseName);

            normalizeZipEntries(sourceFile, tempFile);

            if (tempFile.exists() && tempFile.length() > 0) {
                if (cachedNormalized.exists()) {
                    cachedNormalized.delete();
                }
                if (tempFile.renameTo(cachedNormalized)) {
                    Log.i(TAG, "Successfully created normalized ROM: " + cachedNormalized.getAbsolutePath());
                    return cachedNormalized;
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Error normalizing ROM " + sourceFile.getName() + ": " + e.getMessage(), e);
        }

        return sourceFile;
    }

    /**
     * Checks if the zip contains MAME 8.3 naming (e.g. 201.c1) without the FBNeo hyphen naming (201-c1.c1).
     */
    public static boolean needsNormalization(File zipFile) {
        if (!zipFile.exists() || zipFile.length() == 0) return false;

        try (ZipFile z = new ZipFile(zipFile)) {
            Set<String> names = new HashSet<>();
            Enumeration<? extends ZipEntry> entries = z.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                names.add(entry.getName());
            }

            // Check for Neo Geo BIOS missing aliases
            if (zipFile.getName().equalsIgnoreCase("neogeo.zip")) {
                if (names.contains("asia-s3.rom") && !names.contains("sp-s3.sp1")) return true;
                if (names.contains("sp-u2.sp1") && !names.contains("usa_2slt.bin")) return true;
                if (names.contains("sp-s2.sp1") && !names.contains("neo-geo.rom")) return true;
                return false;
            }

            // Check for game ROM naming mismatches
            for (String name : names) {
                Matcher m = NEO_ROM_PATTERN.matcher(name);
                if (m.matches()) {
                    String baseId = m.group(1);
                    String ext = m.group(2);
                    String expectedAlias = baseId + "-" + ext + "." + ext;
                    if (!names.contains(expectedAlias)) {
                        return true;
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "Failed reading zip entries for " + zipFile.getName() + ": " + e.getMessage());
        }

        return false;
    }

    /**
     * Copies entries from source to dest, injecting FBNeo aliases for any matching MAME names.
     */
    public static void normalizeZipEntries(File srcZip, File dstZip) throws Exception {
        byte[] buffer = new byte[65536];
        Set<String> writtenNames = new HashSet<>();

        try (ZipFile zin = new ZipFile(srcZip);
             ZipOutputStream zout = new ZipOutputStream(new BufferedOutputStream(new FileOutputStream(dstZip)))) {

            // Use fast deflate compression for speed
            zout.setLevel(1);

            Enumeration<? extends ZipEntry> entries = zin.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                String name = entry.getName();

                if (!writtenNames.contains(name)) {
                    writtenNames.add(name);
                    zout.putNextEntry(new ZipEntry(name));
                    try (InputStream in = new BufferedInputStream(zin.getInputStream(entry))) {
                        int len;
                        while ((len = in.read(buffer)) > 0) {
                            zout.write(buffer, 0, len);
                        }
                    }
                    zout.closeEntry();
                }

                // Generate and inject aliases
                Matcher m = NEO_ROM_PATTERN.matcher(name);
                if (m.matches()) {
                    String baseId = m.group(1);
                    String ext = m.group(2);
                    String alias = baseId + "-" + ext + "." + ext;

                    writeAliasIfMissing(zin, entry, alias, zout, writtenNames, buffer);

                    if (ext.equalsIgnoreCase("p2")) {
                        writeAliasIfMissing(zin, entry, baseId + "-p2.sp2", zout, writtenNames, buffer);
                        writeAliasIfMissing(zin, entry, baseId + "-p2.ep1", zout, writtenNames, buffer);
                        writeAliasIfMissing(zin, entry, baseId + "-p2.p2", zout, writtenNames, buffer);
                    } else if (ext.equalsIgnoreCase("p3")) {
                        writeAliasIfMissing(zin, entry, baseId + "-p3.sp3", zout, writtenNames, buffer);
                        writeAliasIfMissing(zin, entry, baseId + "-p3.p3", zout, writtenNames, buffer);
                    } else if (ext.equalsIgnoreCase("p4")) {
                        writeAliasIfMissing(zin, entry, baseId + "-p4.sp4", zout, writtenNames, buffer);
                        writeAliasIfMissing(zin, entry, baseId + "-p4.p4", zout, writtenNames, buffer);
                    }
                }

                // Neo Geo BIOS specific aliases
                if (name.equalsIgnoreCase("asia-s3.rom")) {
                    writeAliasIfMissing(zin, entry, "sp-s3.sp1", zout, writtenNames, buffer);
                } else if (name.equalsIgnoreCase("sp-u2.sp1")) {
                    writeAliasIfMissing(zin, entry, "usa_2slt.bin", zout, writtenNames, buffer);
                } else if (name.equalsIgnoreCase("sp-s2.sp1")) {
                    writeAliasIfMissing(zin, entry, "neo-geo.rom", zout, writtenNames, buffer);
                } else if (name.equalsIgnoreCase("sm1.sm1")) {
                    writeAliasIfMissing(zin, entry, "sm1.bin", zout, writtenNames, buffer);
                } else if (name.equalsIgnoreCase("sfix.sfix")) {
                    writeAliasIfMissing(zin, entry, "sfix.bin", zout, writtenNames, buffer);
                }
            }
        }
    }

    private static void writeAliasIfMissing(ZipFile zin, ZipEntry srcEntry, String aliasName,
                                            ZipOutputStream zout, Set<String> writtenNames, byte[] buffer) throws Exception {
        if (writtenNames.contains(aliasName)) return;
        writtenNames.add(aliasName);

        ZipEntry newEntry = new ZipEntry(aliasName);
        zout.putNextEntry(newEntry);
        try (InputStream in = new BufferedInputStream(zin.getInputStream(srcEntry))) {
            int len;
            while ((len = in.read(buffer)) > 0) {
                zout.write(buffer, 0, len);
            }
        }
        zout.closeEntry();
    }
}
