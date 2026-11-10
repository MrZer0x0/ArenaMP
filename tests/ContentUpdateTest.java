import file.ContentUpdate;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.*;

public class ContentUpdateTest {
    private static int checks;
    private static void expect(boolean ok, String message) { checks++; if (!ok) throw new AssertionError(message); }
    private static void write(File file, String text) throws IOException {
        file.getParentFile().mkdirs(); Files.write(file.toPath(), text.getBytes(StandardCharsets.UTF_8));
    }
    private static File zip(File root, String name, String... entries) throws IOException {
        File out = new File(root, name);
        try (ZipOutputStream z = new ZipOutputStream(new FileOutputStream(out))) {
            for (int i = 0; i < entries.length; i += 2) {
                z.putNextEntry(new ZipEntry(entries[i])); z.write(entries[i + 1].getBytes(StandardCharsets.UTF_8)); z.closeEntry();
            }
        }
        return out;
    }
    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("arena-jvm-test").toFile();
        try {
            expect(ContentUpdate.revision("00010").compareTo(ContentUpdate.revision("00009")) > 0, "numeric revisions");
            expect(ContentUpdate.check("\uFEFFversion=00002\nbuild=00001\n").get("version").equals("00002"), "BOM check");
            expect(ContentUpdate.url("t.me/arena_mp").equals("https://t.me/arena_mp"), "URL normalization");
            for (String bad : new String[]{"", "-1", "1.1"}) {
                try { ContentUpdate.revision(bad); throw new AssertionError("bad revision accepted"); }
                catch (IOException expected) { checks++; }
            }
            File data = new File(root, "Data Files"); data.mkdirs();
            File manifest = new File(root, "build.ini");
            String old = "# comment\n[Build]\nformat=1\nversion=00001\nbuild=00005\nurl=\"t.me/arena_mp\"\n[Server]\naddress=178.20.47.31\n[Content]\ncontent=A.esm\ncontent=B.esm\n";
            write(manifest, old); write(new File(data,"Textures/a.dds"),"old");
            File archive = zip(root,"good.zip","Data Files/Textures/a.dds","new","Data Files/sound/test.wav","sound");
            File payload = ContentUpdate.extract(archive,new File(root,"stage1"),new AtomicBoolean(false));
            File journal = new File(root,"journal.properties");
            ContentUpdate.install(payload,data,manifest,"00002",journal);
            expect(ContentUpdate.readText(new File(data,"Textures/a.dds")).equals("new"),"content replaced");
            expect(ContentUpdate.readText(new File(data,"sound/test.wav")).equals("sound"),"new file");
            String result = ContentUpdate.readText(manifest);
            expect(result.contains("version=00002") && result.contains("build=00005"),"independent version stamp");
            expect(result.contains("address=178.20.47.31") && result.contains("content=A.esm\ncontent=B.esm"),"manifest order and server preserved");
            expect(!journal.exists(),"committed journal cleaned");
            ContentUpdate.stampEngineBuild(manifest, "00006");
            String engineStamped = ContentUpdate.readText(manifest);
            expect(engineStamped.contains("version=00002") && engineStamped.contains("build=00006"), "APK stamp only advances engine");
            expect(engineStamped.contains("# comment") && engineStamped.contains("address=178.20.47.31")
                && engineStamped.contains("content=A.esm\ncontent=B.esm"), "APK stamp preserves endpoint, comments and plugin order");
            try { ContentUpdate.stampEngineBuild(manifest, "broken"); throw new AssertionError("invalid build accepted"); }
            catch (IOException expected) { checks++; }
            expect(ContentUpdate.readText(manifest).equals(engineStamped), "failed APK stamp preserves manifest");
            for (String path : new String[]{"../escape", "/absolute", "C:/evil", "a/../../evil", "test:ads"}) {
                try {
                    ContentUpdate.extract(zip(root,"bad.zip",path,"bad"),new File(root,"badstage"+checks),new AtomicBoolean(false));
                    throw new AssertionError("unsafe ZIP accepted");
                } catch (IOException expected) { checks++; }
            }
            try {
                ContentUpdate.extract(zip(root,"case.zip","Textures/a","a","textures/A","b"),new File(root,"case"),new AtomicBoolean(false));
                throw new AssertionError("case collision accepted");
            } catch (IOException expected) { checks++; }
            try {
                ContentUpdate.extract(archive,new File(root,"canceled"),new AtomicBoolean(true));
                throw new AssertionError("canceled extraction ran");
            } catch (InterruptedIOException expected) { checks++; }
            // Reject a destination conflict before touching either content or the revision.
            write(manifest,old);write(new File(data,"a"),"original");new File(data,"z-blocked").mkdirs();
            File failedPayload=ContentUpdate.extract(zip(root,"fail.zip","a","replacement","z-blocked","invalid"),new File(root,"stage2"),new AtomicBoolean(false));
            try {ContentUpdate.install(failedPayload,data,manifest,"00003",journal);throw new AssertionError("install should fail");}
            catch(IOException expected){checks++;}
            expect(ContentUpdate.readText(new File(data,"a")).equals("original"),"rollback restores overwritten file");
            expect(ContentUpdate.readText(manifest).equals(old),"rollback preserves revision");
            // Emulate an interrupted process after its backup rename and partial replacement.
            File dest=new File(data,"interrupted");File backup=new File(data,"interrupted.arena-backup");
            write(dest,"partial");write(backup,"before crash");
            Properties p=new Properties();p.setProperty("state","applying");p.setProperty("count","1");
            p.setProperty("0.dest",dest.getAbsolutePath());p.setProperty("0.backup",backup.getAbsolutePath());
            p.setProperty("0.temp",new File(data,"interrupted.arena-new").getAbsolutePath());
            p.setProperty("0.original","true");p.setProperty("0.started","true");
            try(OutputStream out=new FileOutputStream(journal)){p.store(out,"");}
            ContentUpdate.recover(journal);ContentUpdate.recover(journal);
            expect(ContentUpdate.readText(dest).equals("before crash"),"idempotent crash recovery");
            System.out.println("ContentUpdate JVM: " + checks + " checks passed");
        } finally { ContentUpdate.deleteTree(root); }
    }
}
