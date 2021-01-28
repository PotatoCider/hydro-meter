const express = require('express')
const cors = require('cors')
const Redis = require("ioredis")
const redis = new Redis({ keyPrefix: 'hydro-rpg:' })
const app = express()
const port = 3000

app.use(express.json())
app.use(cors())

// app.get('/', (req, res) => {
//     res.send('Hello World!')
// })

app.post('/signup', async (req, res) => {
    console.log('received POST /signup')
    console.log('body:', req.body)
    let { chip_id } = req.body
    if (chip_id <= 0 || chip_id >= 2 ** 32)
        return res.status(400).send('Invalid request: chip_id is out of bounds')

    let user_id
    do {
        user_id = Math.floor(Math.random() * 2 ** 32)
    } while (user_id === 0 || await redis.exists(`users:${user_id}`))
    const joined_timestamp = Date.now()
    redis.hset(`${chip_id}:${user_id}`,
        'total_flow_volume', 0,
        'last_flow_volume', 0,
    )
    redis.hset(`users:${user_id}`,
        'chip_id', chip_id,
        'joined_timestamp', joined_timestamp,
    )
    res.status(200).send(JSON.stringify({ chip_id, user_id, joined_timestamp }))
})

app.get('/users', async (req, res) => {
    console.log('received GET /users')
    console.log('query:', req.query)
    let { user_id } = req.body
    const chip_id = redis.hget(`users:${user_id}`, 'chip_id')
    if (!chip_id)
        return res.status(400).send('Unable to find chip_id. User not signed up?')
    res.status(200).send(JSON.stringify({
        user_id, chip_id,
        joined_timestamp: await redis.hget(`users:${user_id}`, 'joined_timestamp')
    }))
})

app.get('/flow_volume', async (req, res) => {
    console.log('received GET /flow_volume')
    console.log('query:', req.query)
    const { chip_id, user_id } = req.query
    if (chip_id <= 0 || chip_id >= 2 ** 32) {
        res.status(400).send('Invalid request: chip_id is out of bounds')
        return
    }
    if (user_id <= 0 || user_id >= 2 ** 32) {
        res.status(400).send('Invalid request: user_id is out of bounds')
        return
    }
    const key = `${chip_id}:${+user_id}`
    const dailyKey = `${chip_id}:${+user_id}:daily_flow_volume`
    const weeklyKey = `${chip_id}:${+user_id}:weekly_flow_volume`
    const monthlyKey = `${chip_id}:${+user_id}:monthly_flow_volume`
    const yearlyKey = `${chip_id}:${+user_id}:yearly_flow_volume`
    // const lastKey = `${chip_id}:${+user_id}:last_flow_volume`

    res.send({
        chip_id, user_id,
        total_flow_volume: +(await redis.hget(key, 'total_flow_volume')), // +null === 0
        daily_flow_volume: +(await redis.get(dailyKey)),
        weekly_flow_volume: +(await redis.get(weeklyKey)),
        monthly_flow_volume: +(await redis.get(monthlyKey)),
        yearly_flow_volume: +(await redis.get(yearlyKey)),
        last_flow_volume: +(await redis.hget(key, 'last_flow_volume')),
        last_timestamp: +(await redis.hget(key, 'last_timestamp')),
    })
})

app.post('/flow_volume', async (req, res) => {
    console.log('received POST /flow_volume')
    console.log('body:', req.body)
    const { chip_id, user_id, flow_volume } = req.body
    if (isNaN(flow_volume)) return res.status(400).send('Invalid flow_volume')
    const key = `${chip_id}:${+user_id}`
    // const totalKey = `${chip_id}:${+user_id}:total_flow_volume`
    const dailyKey = `${chip_id}:${+user_id}:daily_flow_volume`
    const weeklyKey = `${chip_id}:${+user_id}:weekly_flow_volume`
    const monthlyKey = `${chip_id}:${+user_id}:monthly_flow_volume`
    const yearlyKey = `${chip_id}:${+user_id}:yearly_flow_volume`
    // const lastKey = `${chip_id}:${user_id}:last_flow_volume`
    const now = new Date()
    const expireDay = new Date()
    const expireWeek = new Date()
    const expireMonth = new Date()
    const expireYear = new Date()
    expireDay.setHours(24, 0, 0, 0) // next midnight

    expireWeek.setDate(now.getDate() + (7 - now.getDay()) % 7) // set to Sunday
    expireWeek.setHours(24, 0, 0, 0)

    expireMonth.setMonth(now.getMonth() + 1, 1)
    expireMonth.setHours(0, 0, 0, 0)
    expireYear.setFullYear(now.getFullYear() + 1, 0, 1)
    expireYear.setHours(0, 0, 0, 0)

    redis.hincrbyfloat(key, 'total_flow_volume', flow_volume)
    redis.incrbyfloat(dailyKey, flow_volume)
    redis.incrbyfloat(weeklyKey, flow_volume)
    redis.incrbyfloat(monthlyKey, flow_volume)
    redis.incrbyfloat(yearlyKey, flow_volume)
    const last_unix = await redis.hget(key, 'last_timestamp')
    const now_unix = Math.floor(now.getTime() / 1000)
    if (now_unix - last_unix < 300) // 5 min
        redis.hincrbyfloat(key, 'last_flow_volume', flow_volume)
    else
        redis.hset(key, 'last_flow_volume', flow_volume)

    redis.hset(key, 'last_timestamp', now_unix)

    redis.expireat(dailyKey, Math.floor(expireDay.getTime() / 1000))
    redis.expireat(weeklyKey, Math.floor(expireWeek.getTime() / 1000))
    redis.expireat(monthlyKey, Math.floor(expireMonth.getTime() / 1000))
    redis.expireat(yearlyKey, Math.floor(expireYear.getTime() / 1000))

    res.status(200).send('200 OK')
})

app.listen(port, () => {
    console.log(`Listening at http://localhost:${port}`)
})
