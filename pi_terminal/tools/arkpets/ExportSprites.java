// Offline asset preparation only. Uses separately supplied Spine 3.8/libGDX jars.
// See README.md for dependency licenses and model copyright restrictions.
import com.badlogic.gdx.ApplicationAdapter;
import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.backends.lwjgl3.Lwjgl3Application;
import com.badlogic.gdx.backends.lwjgl3.Lwjgl3ApplicationConfiguration;
import com.badlogic.gdx.graphics.*;
import com.badlogic.gdx.graphics.g2d.TextureAtlas;
import com.badlogic.gdx.graphics.glutils.FrameBuffer;
import com.badlogic.gdx.math.Vector2;
import com.badlogic.gdx.utils.FloatArray;
import com.badlogic.gdx.utils.ScreenUtils;
import com.badlogic.gdx.utils.JsonValue;
import com.esotericsoftware.spine.*;
import com.esotericsoftware.spine.utils.TwoColorPolygonBatch;

public class ExportSprites extends ApplicationAdapter {
    static String input, output;
    static final int FPS=24;
    static int SIZE=192;
    static boolean illustration=false;
    static String quality="standard";
    public static void main(String[] args) {
        input=args[0]; output=args[1];
        illustration=args.length>2 && args[2].equals("illustration");
        if(args.length>3)quality=args[3];
        SIZE=quality.equals("economy")?128:quality.equals("high")?384:256;
        Lwjgl3ApplicationConfiguration c=new Lwjgl3ApplicationConfiguration();
        c.setTitle("Offline Amiya sprite exporter"); c.setWindowedMode(SIZE,SIZE);
        c.setInitialVisible(false); c.disableAudio(true);
        new Lwjgl3Application(new ExportSprites(),c);
    }
    @Override public void create() {
        try {
            TextureAtlas atlas=new TextureAtlas(Gdx.files.absolute(input+".atlas"));
            var skeletonFile=Gdx.files.absolute(input+".skel");
            String header=skeletonFile.readString("UTF-8").stripLeading();
            SkeletonData data=header.startsWith("{") ? new SkeletonJson(atlas).readSkeletonData(skeletonFile)
                : new SkeletonBinary(atlas).readSkeletonData(skeletonFile);
            for(Animation a:data.getAnimations()) System.out.println("ANIMATION "+a.getName()+" "+a.getDuration());
            String idle=find(data,"(?i)^(Relax|Id.?le)([_0-9].*)?$");
            if(idle==null && illustration) for(Animation a:data.getAnimations()) {
                if(a.getDuration()>0 && a.getDuration()<=120){idle=a.getName();break;}
            }
            if(idle==null) throw new IllegalArgumentException("No supported idle animation");
            String move=find(data,"(?i)^Move([_0-9].*)?$");
            String interact=find(data,"(?i)^(Interact|Attack|Combat)([_0-9].*)?$");
            String sit=find(data,"(?i)^Sit(_Loop)?$");
            String sleep=find(data,"(?i)^(Sleep|Lie)(_Loop)?$");
            String[] names={idle,move==null?idle:move,interact==null?idle:interact,sit==null?idle:sit,sleep==null?idle:sleep};
            String[] keys={"idle","walk","interact","sit","sleep"};
            int[] rates=new int[5],counts=new int[5];
            int columns=Math.min(8,2048/SIZE);
            int budgetMiB=quality.equals("economy")?64:quality.equals("high")?256:128;
            int maxFrames=Math.min(240,Math.min((8192/SIZE)*columns,
                (budgetMiB*1024*1024/(5*SIZE*SIZE*4)/columns)*columns));
            double[] sampleRates=new double[5];
            for(int index=0;index<names.length;index++) {
                float duration=data.findAnimation(names[index]).getDuration();
                if(duration<=0 || duration>120)throw new IllegalArgumentException("Animation duration outside 0..120 seconds");
                counts[index]=Math.max(1,Math.min(maxFrames,(int)Math.ceil(duration*FPS)));
                sampleRates[index]=counts[index]/(double)duration;
            }
            // Quality fixes spatial resolution. Only temporal sampling adapts
            // to bounded memory; preserve the complete original loop duration.
            // Measure the union of ALL sampled poses: one common ground/pivot,
            // no cropping or scale change when switching animations.
            float minX=Float.MAX_VALUE,minY=Float.MAX_VALUE,maxX=-Float.MAX_VALUE,maxY=-Float.MAX_VALUE;
            for(int index=0;index<names.length;index++) {
                String name=names[index];
                Animation a=data.findAnimation(name);
                if(a==null) throw new IllegalArgumentException("Missing animation: "+name);
                Skeleton s=new Skeleton(data);
                for(int f=0;f<counts[index];f++) {
                    s.setToSetupPose();a.apply(s,0,(float)(f/sampleRates[index]),false,null,1,Animation.MixBlend.replace,Animation.MixDirection.in);
                    s.updateWorldTransform();Vector2 o=new Vector2(),z=new Vector2();s.getBounds(o,z,new FloatArray());
                    minX=Math.min(minX,o.x);minY=Math.min(minY,o.y);maxX=Math.max(maxX,o.x+z.x);maxY=Math.max(maxY,o.y+z.y);
                }
            }
            float scale=(SIZE-8)/Math.max(maxX-minX,maxY-minY);
            OrthographicCamera camera=new OrthographicCamera(SIZE,SIZE);
            camera.position.set((minX+maxX)*scale/2,(minY+maxY)*scale/2,0);camera.update();
            FrameBuffer fb=new FrameBuffer(Pixmap.Format.RGBA8888,SIZE,SIZE,false);
            TwoColorPolygonBatch batch=new TwoColorPolygonBatch(); batch.setProjectionMatrix(camera.combined);
            SkeletonRenderer renderer=new SkeletonRenderer();renderer.setPremultipliedAlpha(true);
            var animations=new JsonValue(JsonValue.ValueType.object);
            for(int index=0;index<names.length;index++) {
                String name=names[index];
                Animation a=data.findAnimation(name);int count=counts[index];
                Skeleton s=new Skeleton(data);s.setScale(scale,scale);
                for(int f=0;f<count;f++) {
                    s.setToSetupPose();a.apply(s,0,(float)(f/sampleRates[index]),false,null,1,Animation.MixBlend.replace,Animation.MixDirection.in);s.updateWorldTransform();
                    fb.begin();ScreenUtils.clear(0,0,0,0);batch.begin();renderer.draw(batch,s);batch.end();
                    Pixmap p=Pixmap.createFromFrameBuffer(0,0,SIZE,SIZE);
                    PixmapIO.writePNG(Gdx.files.absolute(output+"/"+keys[index]+"/"+String.format("%03d",f)+".png"),p,-1,true);
                    p.dispose();fb.end();
                }
                System.out.println("EXPORTED "+name+" "+count);
                var animation=new JsonValue(JsonValue.ValueType.object);
                animation.addChild("originalAnimation",new JsonValue(name));
                animation.addChild("frames",new JsonValue(count));animation.addChild("fps",new JsonValue(sampleRates[index]));
                animation.addChild("duration",new JsonValue(a.getDuration()));
                animation.addChild("supported",new JsonValue(index==0 || (index==1?move!=null:index==2?interact!=null:index==3?sit!=null:sleep!=null)));
                animations.addChild(keys[index],animation);
            }
            var info=new JsonValue(JsonValue.ValueType.object);
            info.addChild("frameWidth",new JsonValue(SIZE));info.addChild("frameHeight",new JsonValue(SIZE));
            info.addChild("footY",new JsonValue(Math.max(0,Math.min(SIZE,SIZE/2+(minY+maxY)*scale/2))));
            info.addChild("columns",new JsonValue(columns));info.addChild("actions",animations);
            info.addChild("conversionVersion",new JsonValue(4));
            info.addChild("quality",new JsonValue(quality));info.addChild("textureBudgetMiB",new JsonValue(budgetMiB));
            info.addChild("displayWidth",new JsonValue(192));info.addChild("displayHeight",new JsonValue(192));
            Gdx.files.absolute(output+"/export.json").writeString(info.toJson(com.badlogic.gdx.utils.JsonWriter.OutputType.json),false,"UTF-8");
            batch.dispose();fb.dispose();atlas.dispose();Gdx.app.exit();
        } catch(Exception e) { e.printStackTrace();System.exit(1); }
    }
    static String find(SkeletonData data,String regex) {
        for(Animation a:data.getAnimations()) if(a.getName().matches(regex)) return a.getName();
        return null;
    }
}
