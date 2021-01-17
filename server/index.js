const express = require('express')
const app = express()
const port = 3000

app.use(express.json())

app.get('/', (req, res) => {
    res.send('Hello World!')
})

app.post('/flow_rate', (req, res) => {
    console.log('received POST /flow_rate')
    console.log('flow_rate:', req.body)
    res.status(200)
    res.end()
})

app.listen(port, () => {
    console.log(`Listening at http://localhost:${port}`)
})
