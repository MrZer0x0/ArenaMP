import file.AssetTransaction;
import file.ContentUpdate;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
public class AssetTransactionTest {
    static int checks;
    static void check(boolean value,String message){checks++;if(!value)throw new AssertionError(message);}
    static void write(File file,String text)throws IOException{file.getParentFile().mkdirs();Files.write(file.toPath(),text.getBytes(StandardCharsets.UTF_8));}
    public static void main(String[] args)throws Exception{
        File root=Files.createTempDirectory("arena-assets-test").toFile();
        try{
            write(new File(root,"resources/obsolete"),"old");write(new File(root,"resources/ui"),"old");
            write(new File(root,"config/defaults.bin"),"old");write(new File(root,"userdata/save"),"SAVE");
            write(new File(root,"server/data/player/alice.json"),"PLAYER");
            File stage=new File(root,"stage");write(new File(stage,"resources/ui"),"new");write(new File(stage,"config/defaults.bin"),"new defaults");
            AssetTransaction.apply(root,stage,Arrays.asList("resources","config"),"assets.sha256","hash-2");
            check(ContentUpdate.readText(new File(root,"resources/ui")).equals("new"),"new assets installed");
            check(!new File(root,"resources/obsolete").exists(),"obsolete assets removed");
            check(ContentUpdate.readText(new File(root,"config/defaults.bin")).equals("new defaults"),"global defaults updated");
            check(ContentUpdate.readText(new File(root,"userdata/save")).equals("SAVE"),"saved games preserved");
            check(ContentUpdate.readText(new File(root,"server/data/player/alice.json")).equals("PLAYER"),"server players preserved");
            check(ContentUpdate.readText(new File(root,"assets.sha256")).equals("hash-2"),"stamp committed last");
            // Fail after first managed directory is swapped: second source deliberately absent.
            write(new File(stage,"resources/ui"),"broken");
            try{AssetTransaction.apply(root,stage,Arrays.asList("resources","missing"),"assets.sha256","hash-3");throw new AssertionError("should fail");}
            catch(IOException expected){checks++;}
            check(ContentUpdate.readText(new File(root,"resources/ui")).equals("new"),"asset tree rolled back");
            check(ContentUpdate.readText(new File(root,"assets.sha256")).equals("hash-2"),"failed install did not stamp assets");
            // Server directory updates share the transaction while server/data remains outside it.
            write(new File(stage,"server/scripts/serverCore.lua"),"core-2");write(new File(stage,"resources/version"),"protocol-806");
            AssetTransaction.apply(root,stage,Arrays.asList("server/scripts","resources"),"server-assets.sha256","server-hash");
            check(ContentUpdate.readText(new File(root,"server/data/player/alice.json")).equals("PLAYER"),"server data survives server asset update");
            check(ContentUpdate.readText(new File(root,"server/scripts/serverCore.lua")).equals("core-2"),"server scripts updated");
            AssetTransaction.recover(root);check(!new File(root,".arena-assets-journal").exists(),"journal cleaned");
            System.out.println("AssetTransaction JVM: "+checks+" checks passed");
        }finally{ContentUpdate.deleteTree(root);}
    }
}
