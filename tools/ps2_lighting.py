"""Adapt Leeds' ambient + emissive lighting to SA's native day/night colors.

The source RGB5551 values are lighting contributions, not finished colors.
Keep them in PSM1; derive bounded SA colors using GAME.DTZ's actual timecycle.
SA interpolates two endpoints; intermediate hours/weather remain an approximation.
"""
import json,struct


def export_lighting(game, package):
    c=game.chunk;pointer=c.u32(32+144)
    # CTimeCycle: 16 CVuVectors + six float[16], then uint8[24][8].
    start=pointer+640
    table=[dict(hour=h,weather=w,
                ambient=list(c.span(start+k*192+h*8+w,1)[0] for k in range(3)),
                emissive=list(c.span(start+(6+k)*192+h*8+w,1)[0] for k in range(3)))
           for h in range(24) for w in range(8)]
    day=table[12*8];night=table[0]
    settings=dict(format=1,source='GAME.DTZ CTimeCycle',day=day,night=night,
                  formula='clamp((sourceRGB * ambient / 255 + emissive * 0.5) * (255 / 128 if textured else 1), 0, 255)',
                  nativePipeline='SA CustomBuildingDN 0x53F20098',
                  directionalLighting=False,sourceColorsPreservedInIntermediate=True,
                  intermediateHoursAndWeatherApproximate=True,table=table)
    (package/'lighting-source.json').write_text(json.dumps(settings,indent=2),encoding='utf-8')
    payload=bytes(day['ambient']+day['emissive']+night['ambient']+night['emissive'])
    (package/'intermediate/map-lighting.psl').write_bytes(b'PSL1'+payload)
    return settings


def adapt_colors(colors, endpoint, textured=True):
    output=bytearray(colors)
    for i in range(0,len(colors),4):
        for k in range(3):
            value=colors[i+k]*endpoint['ambient'][k]/255+endpoint['emissive'][k]*.5
            if textured:value*=255/128
            output[i+k]=min(255,int(value+.5))
    return bytes(output)
