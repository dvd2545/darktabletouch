import QtQuick

Canvas {
    id: icon
    property string name: ""
    property color color: "#e6eef0"
    implicitWidth: 24; implicitHeight: 24
    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset(); c.scale(width / 24, height / 24)
        c.strokeStyle = color; c.fillStyle = color; c.lineWidth = 1.8
        c.lineCap = "round"; c.lineJoin = "round"
        function line(points) { c.beginPath(); c.moveTo(points[0], points[1]); for(let i = 2; i < points.length; i += 2) c.lineTo(points[i], points[i+1]); c.stroke() }
        if(name === "light") { c.beginPath(); c.arc(12,12,5,0,Math.PI*2); c.stroke(); for(let i=0;i<8;i++) { const a=i*Math.PI/4; line([12+Math.cos(a)*8,12+Math.sin(a)*8,12+Math.cos(a)*10,12+Math.sin(a)*10]) } }
        else if(name === "color") { for(let i=0;i<3;i++) { c.beginPath(); c.arc(8+i*4, i===1?8:14,5,0,Math.PI*2); c.stroke() } }
        else if(name === "detail") { c.strokeRect(4,4,12,12); line([15,15,21,21]); line([7,10,13,10]); line([10,7,10,13]) }
        else if(name === "compare") { c.strokeRect(3,4,18,16); line([12,4,12,20]); c.fillRect(4,5,7,14) }
        else if(name === "fit") { line([3,9,3,3,9,3]); line([15,3,21,3,21,9]); line([21,15,21,21,15,21]); line([9,21,3,21,3,15]) }
        else if(name === "history") { c.beginPath(); c.arc(12,12,9,0,Math.PI*2); c.stroke(); line([12,6,12,12,16,14]) }
        else if(name === "chevron-right") line([9,5,16,12,9,19])
        else if(name === "chevron-down") line([5,9,12,16,19,9])
        else if(name === "menu") { line([4,6,20,6]); line([4,12,20,12]); line([4,18,20,18]) }
        else if(name === "more") { for(let x = 5; x <= 19; x += 7) { c.beginPath(); c.arc(x,12,1.5,0,Math.PI*2); c.fill() } }
        else if(name === "close" || name === "reject") { line([6,6,18,18]); line([18,6,6,18]) }
        else if(name === "check") line([4,12,9,17,20,6])
        else if(name === "add") { line([5,12,19,12]); line([12,5,12,19]) }
        else if(name === "trash") { line([4,6,20,6]); line([9,3,15,3]); line([6,6,7,21,17,21,18,6]); line([10,10,10,17]); line([14,10,14,17]) }
        else if(name === "export" || name === "import") { line([4,14,4,20,20,20,20,14]); line([12,16,12,3]); line(name === "export" ? [7,8,12,3,17,8] : [7,11,12,16,17,11]) }
        else if(name === "folder") line([3,19,3,5,10,5,12,8,21,8,21,19,3,19])
        else if(name === "crop") { line([6,2,6,18,22,18]); line([2,6,18,6,18,22]) }
        else if(name === "filter") line([3,3,21,3,15,11,15,19,9,22,9,11,3,3])
        else if(name === "copy") { c.strokeRect(8,8,12,13); line([15,8,15,3,3,3,3,16,8,16]) }
        else if(name === "paste") { c.strokeRect(5,5,14,16); c.fillRect(9,3,6,4); line([9,12,15,12]); line([9,16,15,16]) }
        else if(name === "undo" || name === "reset" || name === "redo") { if(name === "redo") { c.translate(24,0); c.scale(-1,1) } line([8,4,3,9,8,14]); c.beginPath(); c.moveTo(3,9); c.bezierCurveTo(22,4,24,21,11,21); c.stroke() }
        else if(name === "star" || name === "unrate") { c.beginPath(); for(let i=0;i<10;i++) { const a=-Math.PI/2+i*Math.PI/5,r=i%2?4:10; const x=12+Math.cos(a)*r,y=12+Math.sin(a)*r; if(i)c.lineTo(x,y);else c.moveTo(x,y) } c.closePath(); c.stroke(); if(name === "unrate") line([3,3,21,21]) }
        else if(name === "gear") {
            c.beginPath(); for(let i=0;i<32;i++) { const a=i*Math.PI/16, r=(i%4===0 || i%4===3)?10:8; const x=12+Math.cos(a)*r,y=12+Math.sin(a)*r; if(i)c.lineTo(x,y);else c.moveTo(x,y) } c.closePath(); c.stroke(); c.beginPath(); c.arc(12,12,3.5,0,Math.PI*2); c.stroke()
        }
        else if(name === "settings") { for(let y=5;y<22;y+=7) line([3,y,21,y]); c.fillRect(7,2,3,6); c.fillRect(15,9,3,6); c.fillRect(9,16,3,6) }
        else if(name === "grid") { for(let x=4;x<20;x+=10) for(let y=4;y<20;y+=10) c.strokeRect(x,y,6,6) }
    }
}
